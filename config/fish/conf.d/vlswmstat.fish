if status is-interactive; and test -t 1; and set -q VLSWM_BASE_TERM
    set -e VLSWM_BASE_TERM
    # foot spawns the shell on an 80x24 pty and resizes it right after the
    # window is mapped; give it a moment so the dashboard draws once, at the
    # final size.
    sleep 0.25
    if command -q vlswmstat
        vlswmstat
    else if test -x $HOME/.local/bin/vlswmstat
        $HOME/.local/bin/vlswmstat
    end
end
