#!/bin/sh
set -eu

PKGDIR=${1:-"$HOME/.local/var/pmbootstrap/cache_git/pmaports/device/testing/linux-postmarketos-qcom-msm8916"}

if [ ! -d "$PKGDIR" ]; then
	echo "Package directory not found: $PKGDIR" >&2
	exit 1
fi

cd "$PKGDIR"
pmbootstrap checksum linux-postmarketos-qcom-msm8916
pmbootstrap build --force linux-postmarketos-qcom-msm8916
