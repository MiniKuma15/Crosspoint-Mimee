#include "Hyphenator.h"

#include <Utf8.h>

#include <algorithm>
#include <cassert>
#include <vector>

#include "HyphenationCommon.h"
#include "LanguageHyphenator.h"
#include "LanguageRegistry.h"
#include "generated/thai_words.h"

#include <cstring>

const LanguageHyphenator* Hyphenator::cachedHyphenator_ = nullptr;

namespace {

// Normalize ISO 639-2 (three-letter) codes to ISO 639-1 (two-letter) codes used by the
// hyphenation registry.  EPUBs may use either form in their dc:language metadata (e.g.
// "eng" instead of "en").  Both the bibliographic ("fre"/"ger") and terminological
// ("fra"/"deu") ISO 639-2 variants are mapped.
struct Iso639Mapping {
  const char* iso639_2;
  const char* iso639_1;
};
static constexpr Iso639Mapping kIso639Mappings[] = {{"eng", "en"}, {"fra", "fr"}, {"fre", "fr"}, {"deu", "de"},
                                                    {"ger", "de"}, {"rus", "ru"}, {"spa", "es"}, {"ita", "it"},
                                                    {"ukr", "uk"}, {"swe", "sv"}, {"fin", "fi"}};

// Maps a BCP-47 or ISO 639-2 language tag to a language-specific hyphenator.
const LanguageHyphenator* hyphenatorForLanguage(const std::string& langTag) {
  if (langTag.empty()) return nullptr;

  // Extract primary subtag and normalize to lowercase (e.g., "en-US" -> "en", "ENG" -> "en").
  std::string primary;
  primary.reserve(langTag.size());
  for (char c : langTag) {
    if (c == '-' || c == '_') break;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    primary.push_back(c);
  }
  if (primary.empty()) return nullptr;

  // Normalize ISO 639-2 three-letter codes to two-letter equivalents.
  for (const auto& mapping : kIso639Mappings) {
    if (primary == mapping.iso639_2) {
      primary = mapping.iso639_1;
      break;
    }
  }

  return getLanguageHyphenatorForPrimaryTag(primary);
}

// Maps a codepoint index back to its byte offset inside the source word.
size_t byteOffsetForIndex(const std::vector<CodepointInfo>& cps, const size_t index) {
  return (index < cps.size()) ? cps[index].byteOffset : (cps.empty() ? 0 : cps.back().byteOffset);
}

// Builds a vector of break information from explicit hyphen markers in the given codepoints.
// Only hyphens that appear between two alphabetic characters are considered valid breaks.
//
// Example: "US-Satellitensystems" (cps: U, S, -, S, a, t, ...)
//   -> finds '-' at index 2 with alphabetic neighbors 'S' and 'S'
//   -> returns one BreakInfo at the byte offset of 'S' (the char after '-'),
//      with requiresInsertedHyphen=false because '-' is already visible.
//
// Example: "Satel\u00ADliten" (soft-hyphen between 'l' and 'l')
//   -> returns one BreakInfo with requiresInsertedHyphen=true (soft-hyphen
//      is invisible and needs a visible '-' when the break is used).
std::vector<Hyphenator::BreakInfo> buildExplicitBreakInfos(const std::vector<CodepointInfo>& cps) {
  std::vector<Hyphenator::BreakInfo> breaks;

  for (size_t i = 1; i + 1 < cps.size(); ++i) {
    const uint32_t cp = cps[i].value;
    if (!isExplicitHyphen(cp) || !isAlphabetic(cps[i - 1].value) || !isAlphabetic(cps[i + 1].value)) {
      continue;
    }
    // Offset points to the next codepoint so rendering starts after the hyphen marker.
    breaks.push_back({cps[i + 1].byteOffset, isSoftHyphen(cp)});
  }

  return breaks;
}

bool isSegmentSeparator(const uint32_t cp) { return isExplicitHyphen(cp) || isApostrophe(cp); }

// Thai script range (consonants, vowels, tone marks, etc). Used only to decide whether
// a word should take the dictionary-segmentation path below; not a full validity check.
bool isThaiCodepoint(const uint32_t cp) {
  return (cp >= 0x0E01 && cp <= 0x0E3A) || (cp >= 0x0E40 && cp <= 0x0E4E);
}

bool isMostlyThaiWord(const std::vector<CodepointInfo>& cps) {
  return !cps.empty() && isThaiCodepoint(cps[0].value);
}

// thai_words.h stores a prefix (front) compressed word list: each record is
// [1 byte shared-prefix-length with the previous word][suffix bytes][0x00], with a
// full (uncompressed) word every kThaiCheckpointStride entries as a "checkpoint" that
// binary search can jump to directly.
//
// IMPORTANT: this decode/lookup path runs very frequently (once per candidate substring
// during Thai line-break search), so it deliberately avoids std::string / heap allocation
// entirely - everything here is stack buffers and raw byte comparisons. An earlier
// std::string-based version of this function was a real performance problem on-device
// (frequent small heap allocations are expensive on a 320KB-RAM MCU and can fragment the
// heap over time), so don't reintroduce std::string/substr/operator+ in this function.
namespace {
// Longest real word in the dictionary is 210 bytes; 256 gives headroom without being wasteful.
constexpr size_t kMaxThaiWordBytes = 256;

int thaiLexCompare(const char* a, size_t aLen, const char* b, size_t bLen) {
  const size_t minLen = aLen < bLen ? aLen : bLen;
  for (size_t i = 0; i < minLen; ++i) {
    const unsigned char ca = static_cast<unsigned char>(a[i]);
    const unsigned char cb = static_cast<unsigned char>(b[i]);
    if (ca != cb) return ca < cb ? -1 : 1;
  }
  if (aLen < bLen) return -1;
  if (aLen > bLen) return 1;
  return 0;
}

// Checkpoints always store shared-prefix-length 0 (a full word), so this can decode
// directly into `out` without needing any previously-decoded word.
void decodeThaiCheckpointInto(size_t checkpointIdx, char* out, size_t& outLen) {
  using namespace ThaiWords;
  size_t i = kThaiCheckpointOffsets[checkpointIdx] + 1;  // +1 skips the shared_len byte
  size_t n = 0;
  while (kThaiWordsBlob[i] != '\0') {
    out[n++] = kThaiWordsBlob[i++];
  }
  outLen = n;
}
}  // namespace

bool thaiWordExists(const char* s, const size_t len) {
  using namespace ThaiWords;
  if (kThaiCheckpointCount == 0) return false;

  char cpBuf[kMaxThaiWordBytes];
  size_t cpLen;
  decodeThaiCheckpointInto(0, cpBuf, cpLen);
  if (thaiLexCompare(s, len, cpBuf, cpLen) < 0) return false;

  size_t lo = 0, hi = kThaiCheckpointCount - 1;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo + 1) / 2;
    decodeThaiCheckpointInto(mid, cpBuf, cpLen);
    if (thaiLexCompare(cpBuf, cpLen, s, len) <= 0) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }

  const size_t blockStartWordIdx = lo * kThaiCheckpointStride;
  const size_t blockEndWordIdx = (lo + 1 < kThaiCheckpointCount)
                                     ? (lo + 1) * kThaiCheckpointStride
                                     : kThaiWordCount;

  char buf[kMaxThaiWordBytes];
  size_t offset = kThaiCheckpointOffsets[lo];
  for (size_t idx = blockStartWordIdx; idx < blockEndWordIdx; ++idx) {
    const uint8_t sharedLen = static_cast<uint8_t>(kThaiWordsBlob[offset]);
    size_t i = offset + 1;
    size_t n = sharedLen;
    while (kThaiWordsBlob[i] != '\0') {
      buf[n++] = kThaiWordsBlob[i++];
    }
    offset = i + 1;

    const int cmp = thaiLexCompare(buf, n, s, len);
    if (cmp == 0) return true;
    if (cmp > 0) return false;
  }
  return false;
}

// Checks whether the dictionary word cps[startCp, startCp+len) exists, given cps/word.
bool thaiSpanExists(const std::string& word, const std::vector<CodepointInfo>& cps, size_t startCp, size_t len) {
  const size_t startByte = cps[startCp].byteOffset;
  const size_t endCpIdx = startCp + len;
  const size_t endByte = (endCpIdx < cps.size()) ? cps[endCpIdx].byteOffset : word.size();
  return thaiWordExists(word.data() + startByte, endByte - startByte);
}

// Given a compound span [spanStartCp, spanEndCp) that greedy longest-match found to be a
// single dictionary word (e.g. "ให้กำลังใจ" or "ทะเลทราย"), look for any INTERNAL cut
// points that also split it into a sequence of smaller valid dictionary words with no
// leftover on either side. A candidate point k only counts if BOTH:
//   - the left part [spanStartCp, k) can be exactly tiled by valid dictionary words, and
//   - the right part [k, spanEndCp) can ALSO be exactly tiled by valid dictionary words
// Requiring both directions is what rules out coincidental short matches that would leave
// a meaningless fragment behind - e.g. for "ขอโทษนะ", "โท" alone is a valid dictionary
// word, but splitting there would leave "ษนะ" as leftover, which is NOT a valid word, so
// that split is correctly rejected. Genuine compounds like "ทะเล"+"ทราย" pass both checks.
void addCompoundInternalBreaks(const std::string& word, const std::vector<CodepointInfo>& cps,
                                const size_t spanStartCp, const size_t spanEndCp,
                                std::vector<Hyphenator::BreakInfo>& breaks) {
  const size_t spanLen = spanEndCp - spanStartCp;
  if (spanLen <= 1) return;
  constexpr size_t kMaxThaiWordCps = 25;

  std::vector<bool> forward(spanLen + 1, false);
  forward[0] = true;
  for (size_t k = 0; k < spanLen; ++k) {
    if (!forward[k]) continue;
    const size_t maxLen = std::min(kMaxThaiWordCps, spanLen - k);
    for (size_t len = 1; len <= maxLen; ++len) {
      if (forward[k + len]) continue;
      if (thaiSpanExists(word, cps, spanStartCp + k, len)) {
        forward[k + len] = true;
      }
    }
  }
  if (!forward[spanLen]) return;  // shouldn't happen (the whole span is itself a valid word), but guard anyway

  std::vector<bool> backward(spanLen + 1, false);
  backward[spanLen] = true;
  for (size_t kk = spanLen; kk-- > 0;) {
    const size_t maxLen = std::min(kMaxThaiWordCps, spanLen - kk);
    for (size_t len = 1; len <= maxLen && !backward[kk]; ++len) {
      if (backward[kk + len] && thaiSpanExists(word, cps, spanStartCp + kk, len)) {
        backward[kk] = true;
      }
    }
  }

  for (size_t k = 1; k < spanLen; ++k) {
    if (forward[k] && backward[k]) {
      breaks.push_back({cps[spanStartCp + k].byteOffset, false});
    }
  }
}

// Longest-match dictionary segmentation for a run of Thai codepoints, with internal
// compound-word boundaries exposed (see addCompoundInternalBreaks above) so the line-fill
// logic (hyphenateWordAtIndex) has every legal break point to pick the widest-fitting one
// from - not just the outer boundaries of whichever dictionary entries matched greedily.
// Returns an empty vector if no dictionary word could be matched anywhere in the text, so
// the caller falls back to the existing every-N-char + hyphen behavior for unknown/foreign
// fragments (e.g. proper nouns, transliterations).
std::vector<Hyphenator::BreakInfo> computeThaiDictionaryBreaks(const std::string& word,
                                                               const std::vector<CodepointInfo>& cps,
                                                               bool& matched) {
  constexpr size_t kMaxThaiWordCps = 25;  // generous cap on longest realistic Thai compound word
  std::vector<Hyphenator::BreakInfo> breaks;
  matched = false;
  size_t i = 0;

  while (i < cps.size()) {
    const size_t maxLen = std::min(kMaxThaiWordCps, cps.size() - i);
    size_t matchedLen = 0;

    for (size_t len = maxLen; len >= 1; --len) {
      if (thaiSpanExists(word, cps, i, len)) {
        matchedLen = len;
        break;
      }
    }

    if (matchedLen == 0) {
      // No dictionary word starts here (foreign char, digit, rare syllable) - skip it,
      // don't record a break at this position.
      ++i;
      continue;
    }

    matched = true;
    addCompoundInternalBreaks(word, cps, i, i + matchedLen, breaks);
    i += matchedLen;
    if (i < cps.size()) {
      breaks.push_back({cps[i].byteOffset, false});
    }
  }

  // An empty `breaks` here with matched=true is valid: the whole run is one
  // dictionary word/chain with no internal split point. Callers must check
  // `matched`, not `breaks.empty()`, to tell "nothing to split" apart from
  // "no dictionary word found at all".
  return breaks;
}

void appendSegmentPatternBreaks(const std::vector<CodepointInfo>& cps, const LanguageHyphenator& hyphenator,
                                const bool includeFallback, std::vector<Hyphenator::BreakInfo>& outBreaks) {
  size_t segStart = 0;

  for (size_t i = 0; i <= cps.size(); ++i) {
    const bool atEnd = i == cps.size();
    const bool atSeparator = !atEnd && isSegmentSeparator(cps[i].value);
    if (!atEnd && !atSeparator) {
      continue;
    }

    if (i > segStart) {
      std::vector<CodepointInfo> segment(cps.begin() + segStart, cps.begin() + i);
      auto segIndexes = hyphenator.breakIndexes(segment);

      if (includeFallback && segIndexes.empty()) {
        const size_t minPrefix = hyphenator.minPrefix();
        const size_t minSuffix = hyphenator.minSuffix();
        for (size_t idx = minPrefix; idx + minSuffix <= segment.size(); ++idx) {
          segIndexes.push_back(idx);
        }
      }

      for (const size_t idx : segIndexes) {
        assert(idx > 0 && idx < segment.size());
        if (idx == 0 || idx >= segment.size()) continue;
        const size_t cpIdx = segStart + idx;
        if (cpIdx < cps.size()) {
          outBreaks.push_back({cps[cpIdx].byteOffset, true});
        }
      }
    }

    segStart = i + 1;
  }
}

void appendApostropheContractionBreaks(const std::vector<CodepointInfo>& cps,
                                       std::vector<Hyphenator::BreakInfo>& outBreaks) {
  constexpr size_t kMinLeftSegmentLen = 3;
  constexpr size_t kMinRightSegmentLen = 3;
  size_t segmentStart = 0;

  for (size_t i = 0; i < cps.size(); ++i) {
    if (isSegmentSeparator(cps[i].value)) {
      if (isApostrophe(cps[i].value) && i > 0 && i + 1 < cps.size() && isAlphabetic(cps[i - 1].value) &&
          isAlphabetic(cps[i + 1].value)) {
        size_t leftPrefixLen = 0;
        for (size_t j = segmentStart; j < i; ++j) {
          if (isAlphabetic(cps[j].value)) {
            ++leftPrefixLen;
          }
        }

        size_t rightSuffixLen = 0;
        for (size_t j = i + 1; j < cps.size() && !isSegmentSeparator(cps[j].value); ++j) {
          if (isAlphabetic(cps[j].value)) {
            ++rightSuffixLen;
          }
        }

        // Avoid stranding short clitics like "l'"/"d'" or contraction tails like "'ve"/"'re"/"'ll".
        if (leftPrefixLen >= kMinLeftSegmentLen && rightSuffixLen >= kMinRightSegmentLen) {
          outBreaks.push_back({cps[i + 1].byteOffset, false});
        }
      }
      segmentStart = i + 1;
    }
  }
}

void sortAndDedupeBreakInfos(std::vector<Hyphenator::BreakInfo>& infos) {
  std::sort(infos.begin(), infos.end(), [](const Hyphenator::BreakInfo& a, const Hyphenator::BreakInfo& b) {
    if (a.byteOffset != b.byteOffset) {
      return a.byteOffset < b.byteOffset;
    }
    return a.requiresInsertedHyphen < b.requiresInsertedHyphen;
  });

  infos.erase(std::unique(infos.begin(), infos.end(),
                          [](const Hyphenator::BreakInfo& a, const Hyphenator::BreakInfo& b) {
                            return a.byteOffset == b.byteOffset;
                          }),
              infos.end());
}

}  // namespace

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string& word, const bool includeFallback) {
  if (word.empty()) {
    return {};
  }

  // Convert to codepoints and normalize word boundaries.
  auto cps = collectCodepoints(word);
  trimSurroundingPunctuationAndFootnote(cps);
  const auto* hyphenator = cachedHyphenator_;

  // Detect apostrophe-like separators early; used by both branches below.
  bool hasApostropheLikeSeparator = false;
  for (const auto& cp : cps) {
    if (isApostrophe(cp.value)) {
      hasApostropheLikeSeparator = true;
      break;
    }
  }

  // Explicit hyphen markers (soft or hard) take precedence over language breaks.
  auto explicitBreakInfos = buildExplicitBreakInfos(cps);
  if (!explicitBreakInfos.empty()) {
    // When a word contains explicit hyphens we also run Liang patterns on each alphabetic
    // segment between them. Without this, "US-Satellitensystems" would only offer one split
    // point (after "US-"), making it impossible to break mid-"Satellitensystems" even when
    // "US-Satelliten-" would fit on the line.
    //
    // Example: "US-Satellitensystems"
    //   Segments: ["US", "Satellitensystems"]
    //   Explicit break: after "US-"           -> @3  (no inserted hyphen)
    //   Pattern breaks on "Satellitensystems" -> @5  Sa|tel  (+hyphen)
    //                                            @8  Satel|li  (+hyphen)
    //                                            @10 Satelli|ten  (+hyphen)
    //                                            @13 Satelliten|sys  (+hyphen)
    //                                            @16 Satellitensys|tems  (+hyphen)
    //   Result: 6 sorted break points; the line-breaker picks the widest prefix that fits.
    if (hyphenator) {
      appendSegmentPatternBreaks(cps, *hyphenator, /*includeFallback=*/false, explicitBreakInfos);
    }
    // Also add apostrophe contraction breaks when present (e.g. "l'état-major"
    // has both an explicit hyphen and an apostrophe that can independently break).
    if (hasApostropheLikeSeparator) {
      appendApostropheContractionBreaks(cps, explicitBreakInfos);
    }
    // Merge all break points into ascending byte-offset order.
    sortAndDedupeBreakInfos(explicitBreakInfos);
    return explicitBreakInfos;
  }

  // Thai has no spaces between words, so long Thai runs arrive here as one giant "word".
  // Use dictionary longest-match to find real word boundaries (no hyphen needed, like CJK)
  // instead of falling straight to the generic every-N-char + hyphen fallback below.
  if (isMostlyThaiWord(cps)) {
    bool thaiMatched = false;
    auto thaiBreaks = computeThaiDictionaryBreaks(word, cps, thaiMatched);
    if (thaiMatched) {
      // Even an empty vector here is correct: it means the whole run is one atomic
      // dictionary word/word-chain with no legal internal break, so this word simply
      // cannot be split (it will wrap as a whole to the next line, same as any other
      // unsplittable token) - do NOT fall through to the generic character-count
      // fallback below, which would butcher it mid-cluster.
      sortAndDedupeBreakInfos(thaiBreaks);
      return thaiBreaks;
    }
    // Truly no dictionary word matched anywhere (e.g. unknown proper noun) - fall
    // through to the existing fallback logic below.
  }

  // Apostrophe-like separators split compounds into alphabetic segments; run Liang on each segment.
  // This allows words like "all'improvviso" to hyphenate within "improvviso" instead of becoming
  // completely unsplittable due to the apostrophe punctuation. Apostrophe contraction breaks are
  // applied regardless of whether a language hyphenator is available.
  if (hasApostropheLikeSeparator) {
    std::vector<BreakInfo> segmentedBreaks;
    if (hyphenator) {
      appendSegmentPatternBreaks(cps, *hyphenator, includeFallback, segmentedBreaks);
    }
    appendApostropheContractionBreaks(cps, segmentedBreaks);
    sortAndDedupeBreakInfos(segmentedBreaks);
    return segmentedBreaks;
  }

  // Ask language hyphenator for legal break points.
  std::vector<size_t> indexes;
  if (hyphenator) {
    indexes = hyphenator->breakIndexes(cps);
  }

  // Only add fallback breaks if needed
  if (includeFallback && indexes.empty()) {
    const size_t minPrefix = hyphenator ? hyphenator->minPrefix() : LiangWordConfig::kDefaultMinPrefix;
    const size_t minSuffix = hyphenator ? hyphenator->minSuffix() : LiangWordConfig::kDefaultMinSuffix;
    for (size_t idx = minPrefix; idx + minSuffix <= cps.size(); ++idx) {
      indexes.push_back(idx);
    }
  }

  if (indexes.empty()) {
    return {};
  }

  std::vector<Hyphenator::BreakInfo> breaks;
  breaks.reserve(indexes.size());
  for (const size_t idx : indexes) {
    // CJK characters can break without inserting a visible hyphen.
    // Check the codepoint at the break position: if it's a CJK character,
    // no hyphen is needed since CJK scripts don't use hyphenation.
    bool needsHyphen = true;
    if (idx < cps.size() && utf8IsCjkBreakable(cps[idx].value)) {
      needsHyphen = false;
    } else if (idx > 0 && utf8IsCjkBreakable(cps[idx - 1].value)) {
      needsHyphen = false;
    }
    breaks.push_back({byteOffsetForIndex(cps, idx), needsHyphen});
  }

  return breaks;
}

void Hyphenator::setPreferredLanguage(const std::string& lang) { cachedHyphenator_ = hyphenatorForLanguage(lang); }
