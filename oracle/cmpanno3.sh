cd /home/stas/Projects/evv-port/build
n=0
while IFS= read -r line; do
  n=$((n+1))
  rm -f xa.wav ya.wav
  timeout 300 wine speak.exe "$line" xa.wav anno 2>/dev/null | grep -E '^speak: (voice )?param|^speak: index' > xa.txt
  timeout 300 wine speak_prim.exe "$line" ya.wav anno 2>/dev/null | grep -E '^speak: (voice )?param|^speak: index' > ya.txt
  if cmp -s xa.wav ya.wav && cmp -s xa.txt ya.txt; then echo "$n same"
  else echo "$n DIFFER"; fi
done < /tmp/say_anno.txt
