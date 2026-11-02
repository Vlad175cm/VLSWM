if vim.fn.argc(-1) ~= 0 then
	return
end

local art = {
	"███╗   ██╗███████╗ ██████╗ ██╗   ██╗██╗███╗   ███╗",
	"████╗  ██║██╔════╝██╔═══██╗██║   ██║██║████╗ ████║",
	"██╔██╗ ██║█████╗  ██║   ██║██║   ██║██║██╔████╔██║",
	"██║╚██╗██║██╔══╝  ██║   ██║╚██╗ ██╔╝██║██║╚██╔╝██║",
	"██║ ╚████║███████╗╚██████╔╝ ╚████╔╝ ██║██║ ╚═╝ ██║",
	"╚═╝  ╚═══╝╚══════╝ ╚═════╝   ╚═══╝  ╚═╝╚═╝     ╚═╝",
}

local width, height = vim.o.columns, vim.o.lines
local buf = vim.api.nvim_create_buf(false, true)
local lines = {}

local row_top = math.floor((height - #art) / 2)
for _ = 1, row_top do
	lines[#lines + 1] = ""
end
for _, raw in ipairs(art) do
	local pad = math.floor(math.max((width - vim.fn.strdisplaywidth(raw)) / 2, 0))
	lines[#lines + 1] = string.rep(" ", pad) .. raw
end
while #lines < height do
	lines[#lines + 1] = ""
end

vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines)
for i = row_top + 1, row_top + #art do
	vim.api.nvim_buf_add_highlight(buf, -1, "SplashTitle", i - 1, 0, -1)
end
vim.api.nvim_win_set_buf(0, buf)
vim.api.nvim_win_set_cursor(0, { row_top + 1, 1 })

for _, key in ipairs({
	"a",
	"b",
	"c",
	"d",
	"e",
	"f",
	"g",
	"h",
	"j",
	"k",
	"l",
	"m",
	"n",
	"o",
	"p",
	"q",
	"r",
	"s",
	"t",
	"u",
	"v",
	"w",
	"x",
	"y",
	"z",
	"/",
	":",
	"?",
	"i",
}) do
	vim.keymap.set("n", key, "<cmd>enew<cr>", { buffer = buf, silent = true, desc = "splash: open empty buffer" })
end
