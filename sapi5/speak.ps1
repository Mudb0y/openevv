# Speak through SAPI itself, with the voice as registered.
#
# voice_test drives the voice directly; this asks Windows. It lists the
# voices SAPI offers, counts the ones that are ours, and has each language's
# first voice say something into a wave file through SAPI's own automation
# object, which is how a program that speaks finds and uses a voice. Run it
# from the thirty-two bit PowerShell under SysWOW64 to ask the other word
# size: each sees only the voices registered for it.
#
#     powershell -File sapi5\speak.ps1 -Expect 96
#     powershell -File sapi5\speak.ps1 -Expect 0     # after unregistering

param(
	# How many OpenEVV voices SAPI should offer: eight a language.
	[Parameter(Mandatory = $true)][int]$Expect,
	# Where the wave files go.
	[string]$Out = '.',
	# Which voices to look through. The machine's, which is what every SAPI
	# program lists, unless a test registered the voice somewhere else.
	[string]$Category = 'HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices'
)

$ErrorActionPreference = 'Stop'
$bits = [IntPtr]::Size * 8

$voice = New-Object -ComObject SAPI.SpVoice
$voices = New-Object -ComObject SAPI.SpObjectTokenCategory
$voices.SetId($Category, $false)
# A token of ours is named for its locale and its preset: OpenEVV-en-US-1.
$ours = @($voices.EnumerateTokens() |
	Where-Object { $_.Id -match '\\OpenEVV-[a-z]{2}-[A-Z]{2}-[1-8]$' })
"$bits bit: SAPI offers $($ours.Count) OpenEVV voices, $Expect wanted"
if ($ours.Count -ne $Expect) { throw "SAPI offers $($ours.Count) OpenEVV voices, not $Expect" }
if ($Expect -eq 0) { exit 0 }

New-Item -ItemType Directory -Force -Path $Out | Out-Null
foreach ($token in $ours | Where-Object { $_.Id -match '-1$' }) {
	$name = $token.GetDescription()
	$language = $token.GetAttribute('Language')
	$file = Join-Path (Resolve-Path $Out) ("$bits-" + ($token.Id -replace '.*\\', '') + '.wav')

	$stream = New-Object -ComObject SAPI.SpFileStream
	# 3 is SSFMCreateForWrite.
	$stream.Open($file, 3, $false)
	$voice.Voice = $token
	$voice.AudioOutputStream = $stream
	[void]$voice.Speak('Hello, this is OpenEVV.', 0)
	$stream.Close()

	$bytes = (Get-Item $file).Length
	"  {0,-48} {1,5}  {2,8} bytes" -f $name, $language, $bytes
	# A header is 44 bytes, and half a second at 11,025 is 11,025 more.
	if ($bytes -lt 11069) { throw "$name said nothing through SAPI" }
}
