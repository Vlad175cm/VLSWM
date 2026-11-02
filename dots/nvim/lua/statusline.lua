local SEV = { ["1"] = "✗", ["2"] = "▲", ["3"] = "»" }

local function statusline()
	local bufnr = vim.api.nvim_get_current_buf()
	local name = vim.api.nvim_buf_get_name(bufnr)
	name = name ~= "" and vim.fn.fnamemodify(name, ":t") or "[No Name]"
	local mod = vim.bo[bufnr].modified and "+" or ""

	local diag = {}
	for sev, ch in pairs(SEV) do
		local n = #vim.diagnostic.get(bufnr, { severity = tonumber(sev) })
		if n > 0 then
			diag[#diag + 1] = ch .. n
		end
	end

	local clients = vim.lsp.get_clients({ bufnr = bufnr })
	local lsp = clients[1] and clients[1].name or ""

	local pos = math.floor(vim.fn.line(".") / math.max(vim.fn.line("$"), 1) * 100)

	return table.concat({
		" ",
		name,
		mod ~= "" and "+" or "",
		#diag > 0 and (" " .. table.concat(diag, " ")) or "",
		"%=",
		lsp ~= "" and (lsp .. " │ ") or "",
		("%d:%d %d%% "):format(vim.fn.line("."), vim.fn.col("."), pos),
	})
end

local function tabline()
	local parts = { " " }
	local cur = vim.api.nvim_get_current_buf()
	for _, b in ipairs(vim.fn.getbufinfo({ buflisted = 1 })) do
		if vim.bo[b.bufnr].buflisted and (vim.api.nvim_buf_is_loaded(b.bufnr) or b.bufnr == cur) then
			local name = b.name ~= "" and vim.fn.fnamemodify(b.name, ":t") or "[No Name]"
			local hlgroup = b.bufnr == cur and "%#TabLineSel#" or "%#TabLine#"
			parts[#parts + 1] = hlgroup .. name .. (vim.bo[b.bufnr].modified and "+" or "")
		end
	end
	return table.concat(parts, "  ") .. "%T%="
end

vim.o.statusline = "%!v:lua.STATUSLINE()"
vim.o.tabline = "%!v:lua.TABLINE()"

function _G.STATUSLINE()
	return statusline()
end

function _G.TABLINE()
	return tabline()
end
