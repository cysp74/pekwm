//
// FontHandler.hh for pekwm
// Copyright (C) 2004-2020 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_FONTHANDLER_HH_
#define _PEKWM_FONTHANDLER_HH_

#include "config.h"

#include "PFont.hh"
#include "Handler.hh"

#include <map>
#include <string>

//! @brief FontHandler, a caching and font type transparent font handler.
class FontHandler {
public:
	FontHandler(bool default_font_x11,
		    const std::string &charset_override);
	~FontHandler(void);

	void setDefaultFontX11(bool default_font_x11) {
		_default_font_x11 = default_font_x11;
	}
	void setCharsetOverride(const std::string &charset_override) {
		_charset_override = charset_override;
	}

	PFont *getFont(const std::string &font);
	void returnFont(PFont *font);

	PFont::Color *getColor(const std::string &color);
	void returnColor(PFont::Color *color);

protected:
	bool parseFontOffset(PFont *pfont, const std::string &str);
	bool parseFontJustify(PFont *pfont, const std::string &str);
	bool replaceFontCharset(std::string &font);

private:
	PFont *newFont(const std::string &font,
		       std::vector<std::string> &tok, PFont::Type &type);
	void parseFontOptions(PFont *pfont,
			      std::vector<std::string> &tok);
	void loadColor(const std::string &color, PFont::Color *font_color,
		       bool fg);

private:
	/** If true, unspecified font type is X11 and not XMB. */
	bool _default_font_x11;
	/** If non empty, override charset in X11/XMB font strings. */
	std::string _charset_override;
	std::vector<HandlerEntry<PFont*> > _fonts;
	std::vector<HandlerEntry<PFont::Color*> > _colors;
};

namespace pekwm
{
	FontHandler* fontHandler();
}

#endif // _PEKWM_FONTHANDLER_HH_
