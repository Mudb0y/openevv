cd /home/stas/Projects/evv-port/build
n=0
while IFS= read -r line; do
  n=$((n+1))
  rm -f xr.wav yr.wav
  timeout 300 wine speak.exe "$line" xr.wav ar 2>/dev/null | grep '^speak: \(voice \)\?param' > xr.txt
  timeout 300 wine speak_prim.exe "$line" yr.wav ar 2>/dev/null | grep '^speak: \(voice \)\?param' > yr.txt
  if cmp -s xr.wav yr.wav && cmp -s xr.txt yr.txt; then echo "$n same"
  else echo "$n DIFFER"; fi
done < /tmp/say_anno.txt
