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
constexpr int gridRowGap = 6;
constexpr int selectedTitleStripHeight = 26;
constexpr int dividerMarginTop = 8;
// Gap between the selected-title strip and line B (grid <-> continue-reading divider)
constexpr int gridToContinueGap = 10;
// How far the continue-reading cover pokes above line B
constexpr int continueReadingOverlap = 16;
// Extra room below the continue-reading cover before line A
constexpr int continueReadingBottomPadding = 24;
// Line A (short divider) insets: left = gap from cover's right edge, right = gap from screen edge
constexpr int lineAInsetLeft = 8;
constexpr int lineAInsetRight = 16;

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
  const int rowHeight = MimeeStyleMetrics::values.homeCoverHeight + 2 * hPaddingInSelection;
  const int gridHeight = gridRows * rowHeight + (gridRows - 1) * gridRowGap;
  const int itemCount =
      std::min(static_cast<int>(recentBooks.size()), MimeeStyleMetrics::values.homeRecentBooksCount);

  // --- Load + draw covers (only on first render; buffer is stored/restored afterwards) ---
  if (!coverRendered) {
    for (int i = 0; i < itemCount; i++) {
      const int col = i % gridColumns;
      const int row = i / gridColumns;
      const int tileX = rect.x + MimeeStyleMetrics::values.contentSidePadding + tileWidth * col;
      const int tileY = rect.y + row * (rowHeight + gridRowGap);

      std::string coverPath = recentBooks[i].coverBmpPath;
      bool hasCover = true;
      const int slotInnerWidth = tileWidth - 2 * hPaddingInSelection;
      const int slotHeight = MimeeStyleMetrics::values.homeCoverHeight;
      if (coverPath.empty()) {
        hasCover = false;
      } else {
        const std::string coverBmpPath =
            UITheme::getCoverThumbPath(coverPath, MimeeStyleMetrics::values.homeCoverHeight);
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            // Fit (not crop): scale the bitmap to slotHeight tall, keep its
            // real aspect ratio, and center it horizontally in the slot. If
            // the resulting width would overflow the slot (unusually wide
            // cover), clamp the width instead so it never bleeds into the
            // next column - only that rare case gets any cropping.
            const float coverW = static_cast<float>(bitmap.getWidth());
            const float coverH = static_cast<float>(bitmap.getHeight());
            const float ratio = coverW / coverH;
            int drawWidth = static_cast<int>(slotHeight * ratio);
            float cropX = 0.0f;
            if (drawWidth > slotInnerWidth) {
              cropX = 1.0f - (static_cast<float>(slotInnerWidth) / static_cast<float>(drawWidth));
              drawWidth = slotInnerWidth;
            }
            const int drawX = tileX + hPaddingInSelection + (slotInnerWidth - drawWidth) / 2;
            renderer.drawBitmap(bitmap, drawX, tileY + hPaddingInSelection, drawWidth, slotHeight, cropX);
          } else {
            hasCover = false;
          }
          file.close();
        }
      }

      renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, tileWidth - 2 * hPaddingInSelection,
                        MimeeStyleMetrics::values.homeCoverHeight, true);

      if (!hasCover) {
        renderer.fillRect(tileX + hPaddingInSelection,
                          tileY + hPaddingInSelection + (MimeeStyleMetrics::values.homeCoverHeight / 3),
                          tileWidth - 2 * hPaddingInSelection, 2 * MimeeStyleMetrics::values.homeCoverHeight / 3, true);
        renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + 20, tileY + hPaddingInSelection + 20, 24);
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

    renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                             Color::LightGray);
    renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection,
                            MimeeStyleMetrics::values.homeCoverHeight, Color::LightGray);
    renderer.fillRectDither(tileX + tileWidth - hPaddingInSelection, tileY + hPaddingInSelection, hPaddingInSelection,
                            MimeeStyleMetrics::values.homeCoverHeight, Color::LightGray);
    renderer.fillRoundedRect(tileX, tileY + MimeeStyleMetrics::values.homeCoverHeight + hPaddingInSelection,
                             tileWidth, hPaddingInSelection, cornerRadius, false, false, true, true, Color::LightGray);
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
        renderer.drawBitmap(bitmap, tileX, coverTopY, coverWidth, coverHeight);
      }
      file.close();
    }
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

  if (x < gridLeft || x >= gridLeft + tileWidth * gridColumns) return false;
  if (y < gridTop || y >= gridTop + gridHeight) return false;

  const int col = (x - gridLeft) / tileWidth;
  const int row = (y - gridTop) / (rowHeight + gridRowGap);
  const int tappedIndex = row * gridColumns + col;

  if (tappedIndex < 0 || tappedIndex >= itemCount) return false;
  index = tappedIndex;
  return true;
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

  // Line C: bottommost divider, full width, right under the icon row.
  const int lineCY = rect.y + MimeeStyleMetrics::values.menuRowHeight;
  renderer.drawLine(rect.x, lineCY, rect.x + rect.width - 1, lineCY, 2, true);
}
