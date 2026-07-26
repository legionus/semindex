# Copyright 2026  Gentoo Authors
# Distributed under the terms of the GNU General Public License v2
EAPI=8

CMAKE_MAKEFILE_GENERATOR=emake
inherit cmake

DESCRIPTION="semindex - semantic indexer for C built on top of Clang/LLVM"
HOMEPAGE="https://github.com/legionus/semindex"

if [[ ${PV} == 9999 ]]; then
	inherit git-r3
	EGIT_BRANCH="master"
	EGIT_REPO_URI="https://github.com/legionus/${PN}.git"
else
	SRC_URI="https://github.com/legionus/${PN}/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"
	KEYWORDS="amd64"
fi

LICENSE="GPL-2"
SLOT="0"
KEYWORDS=""
IUSE=""

DEPEND="
	dev-util/cmake
	dev-db/sqlite
	>=dev-db/sqlite-3.35
	>=dev-lang/clang-21
	>=dev-lang/llvm-21
"
RDEPEND="${DEPEND}"

src_prepare() {
	cmake_src_prepare
}

src_configure() {
	cmake_src_configure
}

src_compile() {
	cmake_src_compile
}

src_install() {
	cmake_src_install
}
