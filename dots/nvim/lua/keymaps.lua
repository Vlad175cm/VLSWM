local map = vim.keymap.set

map("n", "<Esc>", "<cmd>nohlsearch<cr>", { desc = "no highlight" })
map("n", "<C-s>", "<cmd>w<cr>", { desc = "save" })
map({ "i", "n" }, "<C-q>", "<cmd>q<cr>", { desc = "quit window" })

map("n", "<C-h>", "<C-w>h")
map("n", "<C-l>", "<C-w>l")
map("n", "<C-j>", "<C-w>j")
map("n", "<C-k>", "<C-w>k")
map("n", "<C-S-k>", "<cmd>bprevious<cr>")
map("n", "<C-S-j>", "<cmd>bnext<cr>")

map("n", "<leader>,", "<cmd>Telescope find_files<cr>", { desc = "Find files" })
map("n", "<leader>/", "<cmd>Telescope live_grep<cr>", { desc = "Grep" })
map("n", "<leader>.", "<cmd>Telescope buffers<cr>", { desc = "Buffers" })
map("n", "<leader>h", "<cmd>Telescope help_tags<cr>", { desc = "Help" })
map("n", "<leader>o", "<cmd>Telescope oldfiles<cr>", { desc = "Recent files" })

map("n", "gd", function()
	return vim.lsp.buf.definition()
end, { desc = "definition" })
map("n", "gD", function()
	return vim.lsp.buf.declaration()
end, { desc = "declaration" })
map("n", "gr", function()
	return vim.lsp.buf.references()
end, { desc = "references" })
map("n", "gi", function()
	return vim.lsp.buf.implementation()
end, { desc = "implementation" })
map({ "i", "n", "s" }, "<CR>", function()
	if vim.snippet.active() then
		return vim.snippet.jump(1)
	end
	return "<CR>"
end, { expr = true, desc = "jump snippet" })
map({ "n", "v" }, "<leader>ca", function()
	return vim.lsp.buf.code_action()
end, { desc = "code action" })
map("n", "K", function()
	return vim.lsp.buf.hover()
end, { desc = "hover" })

map("n", "v", "p")
map("x", "v", '"_dP')

map("n", "d", '"_x')
map("x", "d", '"_d')

map("n", "<M-Left>", "<Esc>v<Left>")
map("n", "<M-Right>", "<Esc>v<Right>")
map("n", "<M-Up>", "<Esc>v<Up>")
map("n", "<M-Down>", "<Esc>v<Down>")
map("x", "<M-Left>", "<Left>")
map("x", "<M-Right>", "<Right>")
map("x", "<M-Up>", "<Up>")
map("x", "<M-Down>", "<Down>")
