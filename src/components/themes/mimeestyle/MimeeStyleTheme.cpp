#include "MimeeStyleTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
constexpr int gridColumns = 3;
constexpr int gridRows = 2;
constexpr int gridRowGap = 2;
constexpr int selectedTitleStripHeight = 24;
constexpr int dividerMarginTop = 8;
// Gap between the selected-title strip and line B (grid <-> continue-reading divider)
constexpr int gridToContinueGap = 14;
// How far the continue-reading cover pokes above line B
constexpr int continueReadingOverlap = 10;
// Extra room below the continue-reading cover before line A
constexpr int continueReadingBottomPadding = 2;
// Line A (short divider) insets: left = gap from cover's right edge, right = gap from screen edge
constexpr int lineAInsetLeft = 8;
constexpr int lineAInsetRight = 16;
// Real ebook cover aspect ratio (1600x2560), used to give every grid cover
// a fixed, correctly-proportioned width so it fills its border with no
// leftover white margin (instead of centering a variably-sized cover in a
// too-wide slot).
constexpr float kCoverAspect = 1600.0f / 2560.0f;  // width / height

// Same 32px icon set the Home menu already uses (see HomeActivity.cpp /
// LyraTheme.cpp) - duplicated here since LyraTheme's lookup is file-local.
const uint8_t* toolbarIcon(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    default:
      return nullptr;
  }
}
}  // namespace

// ---------------------------------------------------------------------------
// Grid + "selected title" strip
// ---------------------------------------------------------------------------
int MimeeStyleTheme::drawGridAndSelectedTitle(GfxRenderer& renderer, Rect rect,
                                              const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                              bool& coverRendered, bool& coverBufferStored,
                                              std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = (rect.width - 2 * MimeeStyleMetrics::values.contentSidePadding) / gridColumns;
  const int slotHeight = MimeeStyleMetrics::values.homeCoverHeight;
  const int rowHeight = slotHeight + 2 * hPaddingInSelection;
  const int gridHeight = gridRows * rowHeight + (gridRows - 1) * gridRowGap;
  const int itemCount =
      std::min(static_cast<int>(recentBooks.size()), MimeeStyleMetrics::values.homeRecentBooksCount);

  // Fixed cover width for every grid cell, derived from the real ebook
  // cover aspect ratio - NOT from the tile width - so the border/highlight
  // always hugs the cover exactly (no leftover white margin either side).
  const int coverBoxWidth = static_cast<int>(slotHeight * kCoverAspect + 0.5f);

  // --- Load + draw covers (only on first render; buffer is stored/restored afterwards) ---
  if (!coverRendered) {
    for (int i = 0; i < itemCount; i++) {
      const int col = i % gridColumns;
      const int row = i / gridColumns;
      const int tileX = rect.x + MimeeStyleMetrics::values.contentSidePadding + tileWidth * col;
      const int tileY = rect.y + row * (rowHeight + gridRowGap);
      const int coverX = tileX + (tileWidth - coverBoxWidth) / 2;

      std::string coverPath = recentBooks[i].coverBmpPath;
      bool hasCover = true;
      if (coverPath.empty()) {
        hasCover = false;
      } else {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, slotHeight);
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            const float coverW = static_cast<float>(bitmap.getWidth());
            const float coverH = static_cast<float>(bitmap.getHeight());
            const float ratio = coverW / coverH;
            const int naturalWidth = static_cast<int>(slotHeight * ratio);
            if (naturalWidth >= coverBoxWidth) {
              // Slightly wider than our target box (or exactly it) - crop
              // down to coverBoxWidth so it fills the box exactly.
              const float cropX = 1.0f - (static_cast<float>(coverBoxWidth) / static_cast<float>(naturalWidth));
              renderer.drawBitmap(bitmap, coverX, tileY + hPaddingInSelection, coverBoxWidth, slotHeight, cropX);
            } else {
              // Narrower than the box (unusual cover) - draw at its own
              // width, centered, rather than stretching it.
              const int drawX = coverX + (coverBoxWidth - naturalWidth) / 2;
              renderer.drawBitmap(bitmap, drawX, tileY + hPaddingInSelection, naturalWidth, slotHeight);
            }
          } else {
            hasCover = false;
          }
          file.close();
        }
      }

      renderer.drawRect(coverX, tileY + hPaddingInSelection, coverBoxWidth, slotHeight, true);

      if (!hasCover) {
        renderer.fillRect(coverX, tileY + hPaddingInSelection + (slotHeight / 3), coverBoxWidth,
                          2 * slotHeight / 3, true);
        renderer.drawIcon(CoverIcon, coverX + (coverBoxWidth - 24) / 2, tileY + hPaddingInSelection + 20, 24);
      }
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // --- Selection highlight (redrawn every frame, cheap - just borders) ---
  for (int i = 0; i < itemCount; i++) {
    if (selectorIndex != i) continue;
    const int col = i % gridColumns;
    const int row = i / gridColumns;
    const int tileX = rect.x + MimeeStyleMetrics::values.contentSidePadding + tileWidth * col;
    const int tileY = rect.y + row * (rowHeight + gridRowGap);
    const int coverX = tileX + (tileWidth - coverBoxWidth) / 2;

    renderer.fillRoundedRect(coverX - hPaddingInSelection, tileY, coverBoxWidth + 2 * hPaddingInSelection,
                             hPaddingInSelection, cornerRadius, true, true, false, false, Color::LightGray);
    renderer.fillRectDither(coverX - hPaddingInSelection, tileY + hPaddingInSelection, hPaddingInSelection,
                            slotHeight, Color::LightGray);
    renderer.fillRectDither(coverX + coverBoxWidth, tileY + hPaddingInSelection, hPaddingInSelection, slotHeight,
                            Color::LightGray);
    renderer.fillRoundedRect(coverX - hPaddingInSelection, tileY + slotHeight + hPaddingInSelection,
                             coverBoxWidth + 2 * hPaddingInSelection, hPaddingInSelection, cornerRadius, false, false,
                             true, true, Color::LightGray);
  }

  // --- Selected-title strip, directly under the grid, right-aligned ---
  const int titleStripY = rect.y + gridHeight + 4;
  if (itemCount > 0 && selectorIndex >= 0 && selectorIndex < itemCount) {
    const int maxWidth = rect.width - 2 * MimeeStyleMetrics::values.contentSidePadding;
    const auto truncatedTitle =
        renderer.truncatedText(SMALL_FONT_ID, recentBooks[selectorIndex].title.c_str(), maxWidth, EpdFontFamily::BOLD);
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedTitle.c_str(), EpdFontFamily::BOLD);
    const int textX = rect.x + rect.width - MimeeStyleMetrics::values.contentSidePadding - textWidth;
    renderer.drawText(SMALL_FONT_ID, textX, titleStripY + (selectedTitleStripHeight - lineHeight) / 2,
                      truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
  }

  return titleStripY + selectedTitleStripHeight;
}

// ---------------------------------------------------------------------------
// "Continue reading" strip + dividers
// ---------------------------------------------------------------------------
int MimeeStyleTheme::drawContinueReadingStrip(GfxRenderer& renderer, Rect rect, int startY,
                                              const RecentBook& book) const {
  const int tileX = rect.x + MimeeStyleMetrics::values.contentSidePadding;
  const int coverHeight = MimeeStyleMetrics::values.homeCoverHeight;  // same size as grid covers, per design
  const int continueReadingStripHeight = coverHeight + continueReadingBottomPadding;

  // --- Line B: full-width divider between the grid and this strip.
  // Drawn FIRST so the cover (drawn below) visually overlaps it. ---
  const int lineBY = startY + gridToContinueGap;
  renderer.drawLine(rect.x, lineBY, rect.x + rect.width - 1, lineBY, 2, true);

  // Cover is shifted up so it straddles line B (pokes above it).
  const int coverTopY = lineBY - continueReadingOverlap;

  // Fit (not crop): scale to coverHeight tall, keep real aspect ratio.
  int coverWidth = static_cast<int>(coverHeight * 0.6f);  // fallback if no cover art
  bool drewCover = false;

  if (!book.coverBmpPath.empty()) {
    const std::string coverBmpPath =
        UITheme::getCoverThumbPath(book.coverBmpPath, MimeeStyleMetrics::values.homeCoverHeight);
    HalFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const float coverW = static_cast<float>(bitmap.getWidth());
        const float coverH = static_cast<float>(bitmap.getHeight());
        const float ratio = coverW / coverH;
        coverWidth = static_cast<int>(coverHeight * ratio);
        // Paint a solid opaque block first - drawBitmap may skip white
        // source pixels rather than overwrite them, which otherwise lets
        // line B show through the cover where they overlap.
        renderer.fillRect(tileX, coverTopY, coverWidth, coverHeight, false);
        renderer.drawBitmap(bitmap, tileX, coverTopY, coverWidth, coverHeight);
        drewCover = true;
      }
      file.close();
    }
  }
  if (!drewCover) {
    // No cover art (or failed to load) - still paint solid so line B
    // doesn't show through the placeholder box either.
    renderer.fillRect(tileX, coverTopY, coverWidth, coverHeight, false);
  }
  renderer.drawRect(tileX, coverTopY, coverWidth, coverHeight, true);

  // Title + author to the right of the thumbnail
  const int textX = tileX + coverWidth + MimeeStyleMetrics::values.verticalSpacing;
  const int textWidth = rect.width - (textX - rect.x) - MimeeStyleMetrics::values.contentSidePadding;

  auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, 2, EpdFontFamily::BOLD);
  auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);

  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
  const int authorHeight = book.author.empty() ? 0 : (renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2);
  const int totalBlockHeight = titleBlockHeight + authorHeight;

  int textY = coverTopY + (coverHeight - totalBlockHeight) / 2;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, textX, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += titleLineHeight;
  }
  if (!book.author.empty()) {
    textY += renderer.getLineHeight(UI_10_FONT_ID) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, author.c_str(), true);
  }

  // --- Line A: short divider between this strip and the icon toolbar.
  // Inset from the cover's right edge, and stops just short of the
  // screen's right edge (not full width - purely decorative). ---
  const int lineAY = coverTopY + continueReadingStripHeight - dividerMarginTop;
  const int lineAStartX = tileX + coverWidth + lineAInsetLeft;
  const int lineAEndX = rect.x + rect.width - 1 - lineAInsetRight;
  if (lineAEndX > lineAStartX) {
    renderer.drawLine(lineAStartX, lineAY, lineAEndX, lineAY, 1, true);
  }

  return lineAY + dividerMarginTop;
}

// ---------------------------------------------------------------------------
// Entry point called by HomeActivity::render()
// ---------------------------------------------------------------------------
void MimeeStyleTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                          const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                          bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                          std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int afterGridY =
      drawGridAndSelectedTitle(renderer, rect, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                               storeCoverBuffer);

  // "Continue reading" always shows the most-recently-opened book
  // (recentBooks is most-recent-first), regardless of grid selection.
  drawContinueReadingStrip(renderer, rect, afterGridY, recentBooks[0]);
}

// ---------------------------------------------------------------------------
// Touch hit-testing for the grid (mirrors the geometry used above)
// ---------------------------------------------------------------------------
bool MimeeStyleTheme::recentBookIndexFromPoint(Rect rect, const std::vector<RecentBook>& recentBooks, int x, int y,
                                               int& index) const {
  const int itemCount =
      std::min(static_cast<int>(recentBooks.size()), MimeeStyleMetrics::values.homeRecentBooksCount);
  if (itemCount == 0) return false;

  const int tileWidth = (rect.width - 2 * MimeeStyleMetrics::values.contentSidePadding) / gridColumns;
  const int rowHeight = MimeeStyleMetrics::values.homeCoverHeight + 2 * hPaddingInSelection;
  const int gridLeft = rect.x + MimeeStyleMetrics::values.contentSidePadding;
  const int gridTop = rect.y;
  const int gridHeight = gridRows * rowHeight + (gridRows - 1) * gridRowGap;

  if (x >= gridLeft && x < gridLeft + tileWidth * gridColumns && y >= gridTop && y < gridTop + gridHeight) {
    const int col = (x - gridLeft) / tileWidth;
    const int row = (y - gridTop) / (rowHeight + gridRowGap);
    const int tappedIndex = row * gridColumns + col;
    if (tappedIndex < 0 || tappedIndex >= itemCount) return false;
    index = tappedIndex;
    return true;
  }

  // "Continue reading" strip - always recentBooks[0]. Geometry duplicated
  // from drawGridAndSelectedTitle()/drawContinueReadingStrip() (keep the
  // two in sync if those layout constants change). The tappable zone is
  // generous (whole strip band, not just the cover pixels) for an easier
  // touch target.
  const int titleStripY = rect.y + gridHeight + 4;
  const int afterGridY = titleStripY + selectedTitleStripHeight;
  const int lineBY = afterGridY + gridToContinueGap;
  const int coverTopY = lineBY - continueReadingOverlap;
  const int continueReadingBottom = coverTopY + MimeeStyleMetrics::values.homeCoverHeight + continueReadingBottomPadding;
  if (x >= rect.x && x < rect.x + rect.width && y >= coverTopY && y < continueReadingBottom) {
    index = 0;
    return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// Header - identical to LyraTheme::drawHeader, minus the underline (it
// collided with the title text in Home's shorter header rect: see
// MimeeStyleMetrics::values.homeTopPadding).
// ---------------------------------------------------------------------------
void MimeeStyleTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                                 const char* subtitle) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - MimeeStyleMetrics::values.batteryWidth;
  drawBatteryRight(
      renderer,
      Rect{batteryX, rect.y + 5, MimeeStyleMetrics::values.batteryWidth, MimeeStyleMetrics::values.batteryHeight},
      showBatteryPercentage);

  int maxTitleWidth = title != nullptr ? renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD) : 0;
  int maxSubtitleWidth =
      subtitle != nullptr ? renderer.getTextWidth(SMALL_FONT_ID, subtitle, EpdFontFamily::REGULAR) : 0;

  const int availableSpace = rect.width - MimeeStyleMetrics::values.contentSidePadding * 3;
  if (maxTitleWidth + maxSubtitleWidth > availableSpace) {
    if ((maxTitleWidth > availableSpace / 2) && (maxSubtitleWidth > availableSpace / 2)) {
      maxTitleWidth = availableSpace / 2;
      maxSubtitleWidth = availableSpace / 2;
    } else if (maxTitleWidth > maxSubtitleWidth) {
      maxTitleWidth = availableSpace - maxSubtitleWidth;
    } else {
      maxSubtitleWidth = availableSpace - maxTitleWidth;
    }
  }

  if (title) {
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + MimeeStyleMetrics::values.contentSidePadding,
                      rect.y + MimeeStyleMetrics::values.batteryBarHeight + 3, truncatedTitle.c_str(), true,
                      EpdFontFamily::BOLD);
    // NOTE: no underline here on purpose (see comment above).
  }

  if (subtitle) {
    auto truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxSubtitleWidth, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID,
                      rect.x + rect.width - MimeeStyleMetrics::values.contentSidePadding - truncatedSubtitleWidth,
                      rect.y + 50, truncatedSubtitle.c_str(), true);
  }
}

// ---------------------------------------------------------------------------
// Icon toolbar (replaces Lyra's vertical labeled menu list)
// ---------------------------------------------------------------------------
void MimeeStyleTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                     const std::function<std::string(int index)>& buttonLabel,
                                     const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0 || rowIcon == nullptr) return;

  const int slotWidth = rect.width / buttonCount;
  constexpr int iconSize = 32;

  for (int i = 0; i < buttonCount; ++i) {
    const int slotX = rect.x + i * slotWidth;
    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(slotX + 4, rect.y, slotWidth - 8, MimeeStyleMetrics::values.menuRowHeight,
                               cornerRadius, Color::LightGray);
    }

    const UIIcon icon = rowIcon(i);
    const uint8_t* iconBitmap = toolbarIcon(icon);
    if (iconBitmap != nullptr) {
      const int iconX = slotX + (slotWidth - iconSize) / 2;
      const int iconY = rect.y + (MimeeStyleMetrics::values.menuRowHeight - iconSize) / 2;
      renderer.drawIcon(iconBitmap, iconX, iconY, iconSize);
    }
  }
}
