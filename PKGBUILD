# Maintainer: Vlad <https://github.com/Vlad175cm>
pkgname=vlswm-git
pkgver=1.0.0
pkgrel=1
pkgdesc="Minimal fullscreen-stack Wayland compositor with a terminal as the desktop"
arch=('x86_64')
url="https://github.com/Vlad175cm/VLSWM"
license=('MIT')
depends=('wlroots0.20' 'wayland' 'libxkbcommon' 'pixman' 'libinput' 'cairo' 'pango'
         'foot' 'fish' 'xwayland-satellite' 'grim' 'slurp' 'wl-clipboard' 'cliphist'
         'wf-recorder' 'brightnessctl' 'wireplumber')
makedepends=('git' 'cmake' 'gcc' 'pkgconf' 'wayland-protocols' 'wlr-protocols')
provides=('vlswm')
conflicts=('vlswm')
source=("$pkgname::git+$url.git")
sha256sums=('SKIP')

pkgver() {
  cd "$pkgname"
  printf '1.0.0.r%s.%s' "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  cmake -B build -S "$pkgname" -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j"$(nproc)"
}

package() {
  install -Dm755 "$pkgname/build/vlswm" "$pkgdir/usr/bin/vlswm"
  install -Dm755 "$pkgname/build/vlswmstat" "$pkgdir/usr/bin/vlswmstat"
  install -Dm755 "$pkgname/scripts/vlswm-record" "$pkgdir/usr/bin/vlswm-record"
  install -Dm755 "$pkgname/scripts/vlswm-screenshot" "$pkgdir/usr/bin/vlswm-screenshot"
  install -Dm644 "$pkgname/vlswm.desktop" "$pkgdir/usr/share/wayland-sessions/vlswm.desktop"
  install -Dm644 "$pkgname/config/config" "$pkgdir/usr/share/vlswm/config"
  install -Dm644 "$pkgname/config/foot/foot.ini" "$pkgdir/usr/share/vlswm/foot.ini"
  install -Dm644 "$pkgname/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
