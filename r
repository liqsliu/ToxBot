#!/bin/bash



(
cd ~/ToxBot || exit 1
while true; do
r=0
Tgpp
make || exit 1
./toxbot
r=$?
echo "res: $r"
date
if [[ "$r" -eq 143 ]]; then
  echo "killed by pkill"
  echo 'restart ...'
elif [[ "$r" -eq 0 ]]; then
  echo 'wtf ...'
else
  echo 'stop ...'
  break
fi

sleep 1
done

)
