module github.com/DeLFuS77/NXDock

// Only the tools of the releases (tools/releasekey, tools/releasesign) are written in Go;
// NXDock itself is C and C++, built with devkitPro.

go 1.27.0

require (
	golang.org/x/crypto v0.58.0
	golang.org/x/sys v0.50.0
)
