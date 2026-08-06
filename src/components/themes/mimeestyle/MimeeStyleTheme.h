#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Mimee Style theme metrics (zero runtime cost)
// Based on Lyra, but the "recent books" area becomes a 2x3 cover grid
// followed by a compact "continue reading" strip, instead of Lyra's
// single big cover card.
namespace MimeeStyleMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;

  // How many books to load/select in the grid (2 rows x 3 cols)
  v.homeRecentBooksCount = 6;

  // Cover height *inside a grid cell*, and also the continue-reading cover
  // height (same size, per design). Kept as-is per your feedback - only the
  // on-screen white margin around grid covers was the problem, not the size.
  v.homeCoverHeight = 180;

  // More room for the "Bookshelf" header than Lyra's default, so the grid
  // below doesn't creep up into the header text.
  v.homeTopPadding = 66;

  // Total height reserved for: grid (2 rows) + selected-title strip +
  // continue-reading strip + divider lines. Shrunk slightly (via tighter
  // internal gaps, not a smaller cover) to compensate for the taller
  // homeTopPadding above, so the icon toolbar still clears the button-hints
  // bar at the bottom. Tune this on-device to match your screen exactly.
  v.homeCoverTileHeight = 612;

  // Continue-reading title is never shown in the header for this theme
  // (it's shown in its own strip instead)
  v.homeContinueReadingInMenu = false;

  // Gap between the cover-tile area and the icon toolbar below it
  v.homeMenuTopOffset = 8;

  // Height of the icon-toolbar row (repurposed menuRowHeight, since
  // drawButtonMenu below only ever receives 4 items for this theme)
  v.menuRowHeight = 64;

  // Fixed Home header title, matching the mockup ("Bookshelf")
  v.homeStaticTitle = "Bookshelf";

  return v;
}();
}  // namespace MimeeStyleMetrics

class MimeeStyleTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const override;

  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;

  // Same as LyraTheme's header, minus the underline (it collided with the
  // title text in Home's shorter header rect).
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;

  // Hit-tests a touch point against the 2x3 grid (see BaseTheme.h for why
  // this override is required for multi-row cover themes).
  bool recentBookIndexFromPoint(Rect rect, const std::vector<RecentBook>& recentBooks, int x, int y,
                                int& index) const override;

 private:
  // Draws the 2x3 cover grid + the "selected title" strip beneath it.
  // Returns the y-coordinate immediately below the grid+title area.
  int drawGridAndSelectedTitle(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                               int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                               std::function<bool()> storeCoverBuffer) const;

  // Draws the small "continue reading" row (cover thumb + title + author)
  // and the divider line beneath it. Returns the y-coordinate immediately
  // below the divider.
  int drawContinueReadingStrip(GfxRenderer& renderer, Rect rect, int startY, const RecentBook& book) const;
};
