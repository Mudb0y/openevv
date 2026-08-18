cd "$(dirname "$0")/../build"
n=0
while IFS= read -r line; do
  n=$((n+1))
  rm -f xd.wav yd.wav xd.txt yd.txt
  timeout 300 wine speak.exe "$line" xd.wav ard 2>/dev/null | grep -E '^speak: (voice )?param|^speak: index|^speak: (new|set|get|load|delete)Dict' > xd.txt
  timeout 300 wine speak_prim.exe "$line" yd.wav ard 2>/dev/null | grep -E '^speak: (voice )?param|^speak: index|^speak: (new|set|get|load|delete)Dict' > yd.txt
  if cmp -s xd.wav yd.wav && cmp -s xd.txt yd.txt; then echo "$n same"
  else echo "$n DIFFER"; fi
done < /tmp/say.txt
