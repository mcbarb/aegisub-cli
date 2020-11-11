// Copyright (c) 2006, 2007, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file auto4_lua_dialog.cpp
/// @brief Lua 5.1-based scripting engine (configuration-dialogue interface)
/// @ingroup scripting
///

#include "auto4_lua.h"

#include "string_codec.h"

#include <libaegisub/color.h>
#include <libaegisub/log.h>
#include <libaegisub/lua/utils.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/split.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm.hpp>
#include <cfloat>
#include <unordered_map>

using namespace agi::lua;
namespace {
	inline void get_if_right_type(lua_State *L, std::string &def) {
		if (lua_isstring(L, -1))
			def = lua_tostring(L, -1);
	}

	inline void get_if_right_type(lua_State *L, double &def) {
		if (lua_isnumber(L, -1))
			def = lua_tonumber(L, -1);
	}

	inline void get_if_right_type(lua_State *L, int &def) {
		if (lua_isnumber(L, -1))
			def = lua_tointeger(L, -1);
	}

	inline void get_if_right_type(lua_State *L, bool &def) {
		if (lua_isboolean(L, -1))
			def = !!lua_toboolean(L, -1);
	}

	template<class T>
	T get_field(lua_State *L, const char *name, T def) {
		lua_getfield(L, -1, name);
		get_if_right_type(L, def);
		lua_pop(L, 1);
		return def;
	}

	inline std::string get_field(lua_State *L, const char *name) {
		return get_field(L, name, std::string());
	}

	template<class T>
	void read_string_array(lua_State *L, T &cont) {
		lua_for_each(L, [&] {
			if (lua_isstring(L, -1))
				cont.push_back(lua_tostring(L, -1));
		});
	}
	
	enum ButtonIds {
		BTN_OK,
		BTN_YES,
		BTN_SAVE,
		BTN_APPLY,
		BTN_CLOSE,
		BTN_NO,
		BTN_CANCEL,
		BTN_HELP,
		BTN_CONTEXT_HELP
	};

	int string_to_button_id(std::string const& str) {
		static std::unordered_map<std::string, int> ids;
		if (ids.empty()) {
			ids["ok"] = BTN_OK;
			ids["yes"] = BTN_YES;
			ids["save"] = BTN_SAVE;
			ids["apply"] = BTN_APPLY;
			ids["close"] = BTN_CLOSE;
			ids["no"] = BTN_NO;
			ids["cancel"] = BTN_CANCEL;
			ids["help"] = BTN_HELP;
			ids["context_help"] = BTN_CONTEXT_HELP;
		}
		auto it = ids.find(str);
		return it == end(ids) ? -1 : it->second;
	}
}

namespace Automation4 {
	// LuaDialogControl
	LuaDialogControl::LuaDialogControl(lua_State *L)
	// Assume top of stack is a control table (don't do checking)
	: name(get_field(L, "name"))
	, hint(get_field(L, "hint"))
	, x(get_field(L, "x", 0))
	, y(get_field(L, "y", 0))
	, width(get_field(L, "width", 1))
	, height(get_field(L, "height", 1))
	{
		LOG_D("automation/lua/dialog") << "created control: '" << name << "', (" << x << "," << y << ")(" << width << "," << height << "), " << hint;
	}

	namespace LuaControl {
		/// A static text label
		class Label final : public LuaDialogControl {
			std::string label;
		public:
			Label(lua_State *L) : LuaDialogControl(L), label(get_field(L, "label")) { }

			void LuaReadBack(lua_State *L) override {
				// Label doesn't produce output, so let it be nil
				lua_pushnil(L);
			}
		};

		/// A single-line text edit control
		class Edit : public LuaDialogControl {
		protected:
			std::string text;

		public:
			Edit(lua_State *L)
			: LuaDialogControl(L)
			, text(get_field(L, "value"))
			{
				// Undocumented behaviour, 'value' is also accepted as key for text,
				// mostly so a text control can stand in for other things.
				// This shouldn't be exploited and might change later.
				text = get_field(L, "text", text);
			}

			bool CanSerialiseValue() const override { return true; }
			std::string SerialiseValue() const override { return inline_string_encode(text); }
			void UnserialiseValue(const std::string &serialised) override { text = inline_string_decode(serialised); }

			void LuaReadBack(lua_State *L) override {
				lua_pushstring(L, text.c_str());
			}
		};

		/// A color-picker button
		class Color final : public LuaDialogControl {
			agi::Color color;
			bool alpha;

		public:
			Color(lua_State *L, bool alpha)
			: LuaDialogControl(L)
			, color(get_field(L, "value"))
			, alpha(alpha)
			{
			}

			bool CanSerialiseValue() const override { return true; }
			std::string SerialiseValue() const override { return inline_string_encode(color.GetHexFormatted(alpha)); }
			void UnserialiseValue(const std::string &serialised) override { color = inline_string_decode(serialised); }

			void LuaReadBack(lua_State *L) override {
				lua_pushstring(L, color.GetHexFormatted(alpha).c_str());
			}
		};

		/// A multiline text edit control
		class Textbox final : public Edit {
		public:
			Textbox(lua_State *L) : Edit(L) { }
		};

		/// Integer only edit
		class IntEdit final : public Edit {
			int value;
			int min, max;

		public:
			IntEdit(lua_State *L)
			: Edit(L)
			, value(get_field(L, "value", 0))
			, min(get_field(L, "min", INT_MIN))
			, max(get_field(L, "max", INT_MAX))
			{
				if (min >= max) {
					max = INT_MAX;
					min = INT_MIN;
				}
			}

			bool CanSerialiseValue() const override  { return true; }
			std::string SerialiseValue() const override { return std::to_string(value); }
			void UnserialiseValue(const std::string &serialised) override { value = atoi(serialised.c_str()); }

			void LuaReadBack(lua_State *L) override {
				lua_pushinteger(L, value);
			}
		};

		// Float only edit
		class FloatEdit final : public Edit {
			double value;
			double min;
			double max;
			double step;

		public:
			FloatEdit(lua_State *L)
			: Edit(L)
			, value(get_field(L, "value", 0.0))
			, min(get_field(L, "min", -DBL_MAX))
			, max(get_field(L, "max", DBL_MAX))
			, step(get_field(L, "step", 0.0))
			{
				if (min >= max) {
					max = DBL_MAX;
					min = -DBL_MAX;
				}
			}

			bool CanSerialiseValue() const override { return true; }
			std::string SerialiseValue() const override { return std::to_string(value); }
			void UnserialiseValue(const std::string &serialised) override { value = atof(serialised.c_str()); }

			void LuaReadBack(lua_State *L) override {
				lua_pushnumber(L, value);
			}
		};

		/// A dropdown list
		class Dropdown final : public LuaDialogControl {
			std::vector<std::string> items;
			std::string value;

		public:
			Dropdown(lua_State *L)
			: LuaDialogControl(L)
			, value(get_field(L, "value"))
			{
				lua_getfield(L, -1, "items");
				read_string_array(L, items);

				if (items.size() > 0 && std::find(items.begin(), items.end(), value) == items.end()) {
					value = items[0];
				}
			}

			bool CanSerialiseValue() const override { return true; }
			std::string SerialiseValue() const override { return inline_string_encode(value); }
			void UnserialiseValue(const std::string &serialised) override { value = inline_string_decode(serialised); }

			void LuaReadBack(lua_State *L) override {
				lua_pushstring(L, value.c_str());
			}
		};

		class Checkbox final : public LuaDialogControl {
			std::string label;
			bool value;

		public:
			Checkbox(lua_State *L)
			: LuaDialogControl(L)
			, label(get_field(L, "label"))
			, value(get_field(L, "value", false))
			{
			}

			bool CanSerialiseValue() const override { return true; }
			std::string SerialiseValue() const override { return value ? "1" : "0"; }
			void UnserialiseValue(const std::string &serialised) override { value = serialised != "0"; }

			void LuaReadBack(lua_State *L) override {
				lua_pushboolean(L, value);
			}
		};
	}

	// LuaDialog
	LuaDialog::LuaDialog(lua_State *L, bool include_buttons)
	: use_buttons(include_buttons)
	{
		LOG_D("automation/lua/dialog") << "creating LuaDialoug, addr: " << this;

		// assume top of stack now contains a dialog table
		if (!lua_istable(L, 1))
			error(L, "Cannot create config dialog from something non-table");

		// Ok, so there is a table with controls
		lua_pushvalue(L, 1);
		lua_for_each(L, [&] {
			if (!lua_istable(L, -1))
				error(L, "bad control table entry");

			std::string controlclass = get_field(L, "class");
			boost::to_lower(controlclass);

			std::unique_ptr<LuaDialogControl> ctl;

			// Check control class and create relevant control
			if (controlclass == "label")
				ctl = agi::make_unique<LuaControl::Label>(L);
			else if (controlclass == "edit")
				ctl = agi::make_unique<LuaControl::Edit>(L);
			else if (controlclass == "intedit")
				ctl = agi::make_unique<LuaControl::IntEdit>(L);
			else if (controlclass == "floatedit")
				ctl = agi::make_unique<LuaControl::FloatEdit>(L);
			else if (controlclass == "textbox")
				ctl = agi::make_unique<LuaControl::Textbox>(L);
			else if (controlclass == "dropdown")
				ctl = agi::make_unique<LuaControl::Dropdown>(L);
			else if (controlclass == "checkbox")
				ctl = agi::make_unique<LuaControl::Checkbox>(L);
			else if (controlclass == "color")
				ctl = agi::make_unique<LuaControl::Color>(L, false);
			else if (controlclass == "coloralpha")
				ctl = agi::make_unique<LuaControl::Color>(L, true);
			else if (controlclass == "alpha")
				// FIXME
				ctl = agi::make_unique<LuaControl::Edit>(L);
			else
				error(L, "bad control table entry");

			controls.emplace_back(std::move(ctl));
		});

		if (include_buttons && lua_istable(L, 2)) {
			lua_pushvalue(L, 2);
			lua_for_each(L, [&]{
				buttons.emplace_back(-1, check_string(L, -1));
			});
		}

		if (include_buttons && lua_istable(L, 3)) {
			lua_pushvalue(L, 3);
			lua_for_each(L, [&]{
				int id = string_to_button_id(check_string(L, -2));
				std::string label = check_string(L, -1);
				auto btn = boost::find_if(buttons,
					[&](std::pair<int, std::string>& btn) { return btn.second == label; });
				if (btn == end(buttons))
					error(L, "Invalid button for id %s", lua_tostring(L, -2));
				btn->first = id;
			});
		}

		if (buttons.empty()) {
			buttons.emplace_back(BTN_OK, "OK");
			buttons.emplace_back(BTN_CANCEL, "Cancel");
		}

		for (int i = 0; i < buttons.size(); i++) {
			LOG_D("automation/lua/dialog") << "created button: " << buttons[i].second << " (" << i << ")";
		}
	}

	int LuaDialog::LuaReadBack(lua_State *L) {
		// First read back which button was pressed, if any
		if (use_buttons) {
			if (button_pushed == -1 || buttons[button_pushed].first == BTN_CANCEL) {
				LOG_I("agi/auto4_lua_dialog") << "Pushing cancel";
				lua_pushboolean(L, false);
			}
			else {
				const char* s = buttons[button_pushed].second.c_str();
				LOG_I("agi/auto4_lua_dialog") << "Pushing " << s;
				lua_pushstring(L, s);
			}
		}

		// Then read controls back
		lua_createtable(L, 0, controls.size());
		for (auto& control : controls) {
			control->LuaReadBack(L);
			lua_setfield(L, -2, control->name.c_str());
		}

		return use_buttons ? 2 : 1;
	}

	void LuaDialog::PushButton(int button) {
		if (button != -1 && (button < 0 || button >= buttons.size())) {
			LOG_E("agi/auto4_lua_dialog") << "Button " << button << " not in range; defaulting to cancel";
			button = -1;
		}

		button_pushed = button;
	}

	std::string LuaDialog::Serialise() {
		std::string res;

		// Format into "name1:value1|name2:value2|name3:value3"
		for (auto& control : controls) {
			if (control->CanSerialiseValue()) {
				if (!res.empty())
					res += "|";
				res += inline_string_encode(control->name) + ":" + control->SerialiseValue();
			}
		}

		return res;
	}

	void LuaDialog::Unserialise(const std::string &serialised) {
		for (auto tok : agi::Split(serialised, '|')) {
			auto pos = std::find(begin(tok), end(tok), ':');
			if (pos == end(tok)) continue;

			std::string name = inline_string_decode(std::string(begin(tok), pos));
			std::string value(pos + 1, end(tok));

			// Hand value to all controls matching name
			for (auto& control : controls) {
				if (control->name == name && control->CanSerialiseValue())
					control->UnserialiseValue(value);
			}
		}
	}
}
