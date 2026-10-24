$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $run=Join-Path $root ('build\tests\windows-thumbnail-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $run | Out-Null
    Add-Type -AssemblyName System.Drawing
    $png=Join-Path $run 'image.png'
    $image=New-Object System.Drawing.Bitmap 256,256,([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $region=New-Object System.Drawing.Rectangle 0,0,256,256
        $bits=$image.LockBits($region,[System.Drawing.Imaging.ImageLockMode]::WriteOnly,[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        try {$pixels=New-Object byte[] ($bits.Stride*256);(New-Object System.Random 42).NextBytes($pixels);[System.Runtime.InteropServices.Marshal]::Copy($pixels,0,$bits.Scan0,$pixels.Length)} finally {$image.UnlockBits($bits)}
        $image.Save($png,[System.Drawing.Imaging.ImageFormat]::Png)
    } finally {$image.Dispose()}
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\windows_attachment_thumbnail_test.cpp -o (Join-Path $run 'thumbnail-test.exe')
    if($LASTEXITCODE -ne 0){throw 'Thumbnail test compile failed'}
    & (Join-Path $run 'thumbnail-test.exe') $png (Join-Path $run 'thumbnail.jpg')
    if($LASTEXITCODE -ne 0){throw 'Thumbnail validation failed'}
    Write-Host "Evidence: $run"
}finally{Pop-Location}
