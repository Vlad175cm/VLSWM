-- vlswm: black and white colorscheme that follows the VLSWM theme.
-- VLSWM writes the current theme to $XDG_STATE_HOME/vlswm/theme (black/white).

local function state_file()
	local dir = vim.env.XDG_STATE_HOME
	if dir and dir ~= "" then
		return dir .. "/vlswm/theme"
	end
	return vim.env.HOME .. "/.local/state/vlswm/theme"
end

local function current_theme()
	local f = io.open(state_file(), "r")
	if not f then
		return "black"
	end
	local line = f:read("*l") or ""
	f:close()
	return line:match("^%s*white") and "white" or "black"
end

local dark = {
	inv = "#000000",
	fg = "#ffffff",
	dim = "#b4b4b4",
	faint = "#6e6e6e",
	soft = "#171717",
	sel = "#2c2c2c",
	float = "#0b0b0b",
}

local light = {
	inv = "#ffffff",
	fg = "#000000",
	dim = "#4a4a4a",
	faint = "#8c8c8c",
	soft = "#ededed",
	sel = "#d2d2d2",
	float = "#fbfbfb",
}

local theme = current_theme()
local P = theme == "white" and light or dark

vim.o.background = theme == "white" and "light" or "dark"

local hl = vim.api.nvim_set_hl
local none = { bg = "NONE" }

hl(0, "Normal", { fg = P.fg, bg = "NONE" })
hl(0, "NormalNC", { fg = P.fg, bg = "NONE" })
hl(0, "EndOfBuffer", { fg = P.faint, bg = "NONE" })
hl(0, "NonText", { fg = P.faint, bg = "NONE" })

hl(0, "Comment", { fg = P.faint, italic = true })
hl(0, "String", { fg = P.dim })
hl(0, "Character", { fg = P.dim })
hl(0, "Number", { fg = P.fg })
hl(0, "Boolean", { fg = P.fg })
hl(0, "Float", { fg = P.fg })
hl(0, "Constant", { fg = P.fg })
hl(0, "Identifier", { fg = P.fg })
hl(0, "Function", { fg = P.fg, bold = true })
hl(0, "Statement", { fg = P.fg, bold = true })
hl(0, "Keyword", { fg = P.fg, bold = true })
hl(0, "Conditional", { fg = P.fg, bold = true })
hl(0, "Repeat", { fg = P.fg, bold = true })
hl(0, "Label", { fg = P.dim })
hl(0, "Operator", { fg = P.dim })
hl(0, "Exception", { fg = P.fg, underline = true })
hl(0, "PreProc", { fg = P.dim })
hl(0, "Include", { fg = P.dim })
hl(0, "Define", { fg = P.dim })
hl(0, "Macro", { fg = P.dim })
hl(0, "Type", { fg = P.dim })
hl(0, "StorageClass", { fg = P.dim })
hl(0, "Structure", { fg = P.dim })
hl(0, "Special", { fg = P.faint })
hl(0, "SpecialChar", { fg = P.faint })
hl(0, "Delimiter", { fg = P.faint })
hl(0, "MatchParen", { bg = P.sel, fg = P.fg, bold = true })
hl(0, "Todo", { fg = P.fg, bold = true, underline = true })
hl(0, "Underlined", { fg = P.fg, underline = true })
hl(0, "Error", { fg = P.fg, bold = true, underline = true })

hl(0, "StatusLine", { fg = P.dim, bg = "NONE" })
hl(0, "StatusLineNC", { fg = P.faint, bg = "NONE" })
hl(0, "TabLine", { fg = P.faint, bg = "NONE" })
hl(0, "TabLineFill", { fg = P.faint, bg = "NONE" })
hl(0, "TabLineSel", { fg = P.fg, bg = "NONE", bold = true })
hl(0, "MsgArea", { fg = P.fg, bg = "NONE" })
hl(0, "MoreMsg", { fg = P.dim, bg = "NONE", bold = true })
hl(0, "ModeMsg", { fg = P.dim, bg = "NONE", bold = true })
hl(0, "WildMenu", { bg = P.sel, fg = P.fg })
hl(0, "LineNr", { fg = P.faint, bg = "NONE" })
hl(0, "CursorLineNr", { fg = P.fg, bg = "NONE", bold = true })
hl(0, "SignColumn", { fg = P.faint, bg = "NONE" })
hl(0, "ColorColumn", none)
hl(0, "CursorLine", { bg = P.soft })
hl(0, "Visual", { bg = P.sel })
hl(0, "CursorColumn", none)
hl(0, "WinSeparator", { fg = P.faint, bg = "NONE" })
hl(0, "VertSplit", { fg = P.faint, bg = "NONE" })
hl(0, "Folded", { bg = P.soft, fg = P.dim })
hl(0, "FoldColumn", { fg = P.faint, bg = "NONE" })

hl(0, "Pmenu", { bg = P.soft, fg = P.fg })
hl(0, "PmenuSel", { bg = P.fg, fg = P.inv })
hl(0, "PmenuSbar", { bg = P.soft })
hl(0, "PmenuThumb", { bg = P.faint })
hl(0, "PmenuMatch", { fg = P.fg, bold = true })
hl(0, "PmenuMatchSel", { fg = P.inv, bold = true })
hl(0, "NormalFloat", { bg = P.float, fg = P.fg })
hl(0, "FloatBorder", { fg = P.faint, bg = P.float })
hl(0, "FloatTitle", { fg = P.fg, bg = P.float, bold = true })

hl(0, "TelescopeNormal", { bg = P.float, fg = P.fg })
hl(0, "TelescopePromptTitle", { fg = P.fg, bg = P.float, bold = true })
hl(0, "TelescopeResultsTitle", { fg = P.dim, bg = P.float })
hl(0, "TelescopePreviewTitle", { fg = P.dim, bg = P.float })
hl(0, "TelescopePromptBorder", { fg = P.faint, bg = P.float })
hl(0, "TelescopeResultsBorder", { fg = P.faint, bg = P.float })
hl(0, "TelescopePreviewBorder", { fg = P.faint, bg = P.float })
hl(0, "TelescopeSelection", { bg = P.sel })
hl(0, "TelescopeMatching", { fg = P.fg, bold = true, underline = true })

hl(0, "Search", { bg = P.fg, fg = P.inv })
hl(0, "IncSearch", { bg = P.dim, fg = P.inv })
hl(0, "CurSearch", { bg = P.fg, fg = P.inv, bold = true })
hl(0, "Substitute", { bg = P.dim, fg = P.inv })

hl(0, "DiagnosticError", { fg = P.fg, bold = true })
hl(0, "DiagnosticWarn", { fg = P.dim })
hl(0, "DiagnosticInfo", { fg = P.dim })
hl(0, "DiagnosticHint", { fg = P.faint })
hl(0, "DiagnosticUnderlineError", { fg = P.fg, undercurl = true })
hl(0, "DiagnosticUnderlineWarn", { fg = P.dim, undercurl = true })
hl(0, "DiagnosticUnderlineInfo", { fg = P.dim, undercurl = true })
hl(0, "DiagnosticUnderlineHint", { fg = P.faint, undercurl = true })
hl(0, "WarningMsg", { fg = P.fg, bg = "NONE", italic = true })
hl(0, "ErrorMsg", { fg = P.fg, bg = "NONE", bold = true })

hl(0, "GitSignsAdd", { fg = P.fg })
hl(0, "GitSignsChange", { fg = P.dim })
hl(0, "GitSignsDelete", { fg = P.faint })

hl(0, "SplashTitle", { fg = P.fg, bg = "NONE", bold = true })

vim.g.colors_name = "vlswm"
