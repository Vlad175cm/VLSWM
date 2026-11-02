local lazypath = vim.fn.stdpath("data") .. "/lazy/lazy.nvim"
if not (vim.uv or vim.loop).fs_stat(lazypath) then
	vim.fn.system({ "git", "clone", "--filter=blob:none", "--branch=stable", "https://github.com/folke/lazy.nvim.git", lazypath })
	if vim.v.shell_error ~= 0 then
		vim.api.nvim_echo({ { "Failed to clone lazy.nvim:", "ErrorMsg" } }, true, {})
		os.exit(1)
	end
end
vim.opt.rtp:prepend(lazypath)

require("lazy").setup({
	spec = {
		{
			"nvim-treesitter/nvim-treesitter",
			lazy = false,
			build = ":TSUpdate",
			config = function()
				local ts = require("nvim-treesitter")
				ts.setup({})
				ts.install({ "lua", "python", "c", "vimdoc", "markdown" })
				local group = vim.api.nvim_create_augroup("treesitter", { clear = true })
				vim.api.nvim_create_autocmd("FileType", {
					group = group,
					callback = function(args)
						pcall(vim.treesitter.start, args.buf)
					end,
				})
			end,
		},
		{
			"neovim/nvim-lspconfig",
			config = function()
				for _, server in ipairs({ "lua_ls", "pyright", "clangd" }) do
					vim.lsp.enable(server)
				end
			end,
		},
		{
			"nvim-telescope/telescope.nvim",
			dependencies = { "nvim-lua/plenary.nvim" },
			config = function()
				require("telescope").setup({
					defaults = {
						border = false,
						mappings = {
							i = { ["<Esc>"] = require("telescope.actions").close },
						},
					},
				})
			end,
		},
	},
	{
		dev = false,
		install = { colorscheme = { "default" } },
		defaults = { lazy = false },
		performance = {
			rtp = { disabled_plugins = { "gzip", "tarPlugin", "tohtml", "tutor", "zip", "zipPlugin" } },
		},
	},
})
