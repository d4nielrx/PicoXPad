#!/system/bin/sh
for p in /proc/[0-9]*; do
  pid=${p#/proc/}
  for f in "$p"/fd/*; do
    t=$(readlink "$f" 2>/dev/null)
    case "$t" in
      *uinput*|*input/event*) echo "$pid $t";;
    esac
  done
done 2>/dev/null | sort -u
