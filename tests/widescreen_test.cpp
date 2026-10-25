// Checks the pure scaling math from the widescreen engine patch. Run via tests/run.sh.
#include "Common/WideScreen.h"
#include <cassert>
#include <cmath>

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main()
{
	using namespace WideScreen;
	const float tan25 = std::tan(25.0f * 3.14159265f / 180.0f);

	// Retail 4:3: horizontal half-extent is tan(25 deg).
	assert(near(viewPlane(800, 600, 600).halfW, tan25));

	// Resolution does not change the view; 4K shows the same area as 1080p.
	assert(near(viewPlane(3840, 2160, 2160).halfH, viewPlane(1920, 1080, 1080).halfH));
	assert(near(viewPlane(1920, 1080, 1080).halfH, viewPlane(800, 600, 600).halfH));

	// Aspect ratio widens the view instead of zooming in.
	assert(near(viewPlane(1920, 1080, 1080).halfW / viewPlane(1920, 1080, 1080).halfH, 16.0f / 9.0f));
	assert(viewPlane(3440, 1440, 1440).halfW > viewPlane(1920, 1080, 1080).halfW);

	// Showing the control bar (shorter viewport) keeps world scale per pixel.
	assert(near(viewPlane(1920, 864, 1080).halfH / 864.0f, viewPlane(1920, 1080, 1080).halfH / 1080.0f));

	// UI: 16:9 is scaled uniformly and centred; 4:3 is unchanged.
	UiScale s = uiScale(1920, 1080, 800, 600, false);
	assert(near(s.x, 1.8f) && near(s.y, 1.8f) && s.xOffset == 240);
	s = uiScale(1600, 1200, 800, 600, false);
	assert(near(s.x, 2.0f) && s.xOffset == 0);
	// Whole-screen regions still stretch; narrower than 4:3 keeps retail stretch.
	s = uiScale(1920, 1080, 800, 600, true);
	assert(near(s.x, 2.4f) && s.xOffset == 0);
	s = uiScale(1280, 1024, 800, 600, false);
	assert(s.x < s.y && s.xOffset == 0);
	return 0;
}
