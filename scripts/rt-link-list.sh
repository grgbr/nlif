#!/bin/sh -e

exec  out/staging/bin/ynl --spec out/staging/share/ynl/specs/rt-link.yaml --dump getlink
