#!/bin/sh -e

out/staging/bin/ynl --list-families

#out/staging/bin/ynl --spec out/staging/share/ynl/specs/rt-link.yaml --list-msgs
out/staging/bin/ynl --family rt-link --list-msgs

#out/staging/bin/ynl --spec out/staging/share/ynl/specs/rt-link.yaml --doc getlink
out/staging/bin/ynl --family rt-link --doc getlink
