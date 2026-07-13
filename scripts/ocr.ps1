# Windows built-in OCR (Windows.Media.Ocr) over a PNG/JPG/BMP.
# Default: prints one recognized text line per output line.
# -WithBoxes: prints a "#SIZE\t<w>\t<h>" header (bitmap pixels) followed by one
# word per line as "<x>\t<y>\t<w>\t<h>\t<text>" — the word's bounding box in the
# same pixel space, used to recover each token's position on the image.
param(
    [Parameter(Mandatory = $true)][string]$ImagePath,
    [switch]$WithBoxes
)

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime]

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() |
    Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
                   $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]

function Await($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
}

$path = (Resolve-Path $ImagePath).Path
$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($path)) ([Windows.Storage.StorageFile])
$stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
$bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])

$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
if (-not $engine) {
    $lang = New-Object Windows.Globalization.Language 'en'
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)
}
if (-not $engine) { Write-Error 'No OCR language available'; exit 1 }

$result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
if ($WithBoxes) {
    Write-Output ("#SIZE`t{0}`t{1}" -f $bitmap.PixelWidth, $bitmap.PixelHeight)
    foreach ($line in $result.Lines) {
        foreach ($word in $line.Words) {
            $r = $word.BoundingRect
            Write-Output ("{0}`t{1}`t{2}`t{3}`t{4}" -f $r.X, $r.Y, $r.Width, $r.Height, $word.Text)
        }
    }
} else {
    foreach ($line in $result.Lines) { $line.Text }
}
