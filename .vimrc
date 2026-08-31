" Return a list of files matching <path>/*/<fname>
function! FindTags(path, fname)
	return globpath(a:path, '*/' . a:fname, 0, 1)
endfunction

" Load tag files matching <path>/*/tags including tag file found under current
" working directory.
function! LoadCtags(path)
	let l:tags = './tags,tags'
	for l:t in FindTags(a:path, 'tags')
		let l:tags .= ',' . l:t
	endfor
	let &tags = l:tags
endfunction

" Load cscope database files matching <path>/*/cscope.out including database
" found under current working directory.
function! LoadCscope(path)
	if filereadable('cscope.out')
		execute 'cscope add .'
	endif
	for l:f in FindTags(a:path, 'cscope.out')
		execute 'cscope add ' . fnamemodify(l:f, ':h')
	endfor
endfunction

" Probe current directory makefile and return content of BUILDDIR make variable
" if any...
function! GetBuilddir()
	let l:path = system("make -pnqrs help 2>/dev/null | "
	                    \ . "sed -n '/^BUILDDIR/s/[^=]\\+=[[:blank:]]*//p'")
	let l:path = split(l:path)
	if len(l:path) > 0
		return l:path[0]
	else
		return ''
endfunction

let s:builddir = GetBuilddir()
if len(s:builddir) > 0
	" Load ctag files
	:call LoadCtags(s:builddir)
	" Load cscope databases
	:call LoadCscope(s:builddir)
endif
