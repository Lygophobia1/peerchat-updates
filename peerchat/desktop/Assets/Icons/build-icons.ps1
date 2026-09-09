param([string]$OutputDirectory=$PSScriptRoot)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing

function New-BrandBitmap([int]$size,[bool]$server) {
    $bitmap=New-Object -TypeName Drawing.Bitmap -ArgumentList @($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g=[Drawing.Graphics]::FromImage($bitmap);$g.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([Drawing.Color]::Transparent)
    $blue=[Drawing.Drawing2D.LinearGradientBrush]::new([Drawing.Rectangle]::new(0,0,$size,$size),[Drawing.Color]::FromArgb(255,44,141,242),[Drawing.Color]::FromArgb(255,18,75,154),45)
    $radius=[int]($size*.21);$path=[Drawing.Drawing2D.GraphicsPath]::new();$d=$radius*2
    $path.AddArc(0,0,$d,$d,180,90);$path.AddArc($size-$d,0,$d,$d,270,90);$path.AddArc($size-$d,$size-$d,$d,$d,0,90);$path.AddArc(0,$size-$d,$d,$d,90,90);$path.CloseFigure();$g.FillPath($blue,$path)
    $white=[Drawing.SolidBrush]::new([Drawing.Color]::White);$font=[Drawing.Font]::new('Segoe UI',($size*.46),[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
    $g.DrawString('p',$font,$white,[Drawing.PointF]::new($size*.20,$size*.12))
    $tail=[Drawing.PointF[]]@([Drawing.PointF]::new($size*.20,$size*.72),[Drawing.PointF]::new($size*.12,$size*.88),[Drawing.PointF]::new($size*.20,$size*.79));$g.FillPolygon($white,$tail)
    if($server){$rack=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255,145,202,255));for($i=0;$i -lt 3;$i++){ $y=$size*(.55+$i*.14);$g.FillRectangle($rack,$size*.58,$y,$size*.30,$size*.10);$g.FillEllipse([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255,22,77,150)),$size*.63,$y+$size*.035,$size*.025,$size*.025) }}
    $white.Dispose();$font.Dispose();$blue.Dispose();$path.Dispose();$g.Dispose();return $bitmap
}
function Write-Ico([string]$path,[bool]$server){$sizes=16,24,32,48,64,128,256;$pngs=@();foreach($size in $sizes){$bmp=New-BrandBitmap $size $server;$ms=[IO.MemoryStream]::new();$bmp.Save($ms,[Drawing.Imaging.ImageFormat]::Png);$pngs+=,$ms.ToArray();$ms.Dispose();$bmp.Dispose()};$fs=[IO.File]::Create($path);$bw=[IO.BinaryWriter]::new($fs);$bw.Write([UInt16]0);$bw.Write([UInt16]1);$bw.Write([UInt16]$pngs.Count);$offset=6+16*$pngs.Count;for($i=0;$i -lt $pngs.Count;$i++){$size=$sizes[$i];$bw.Write([byte]($(if($size -eq 256){0}else{$size})));$bw.Write([byte]$(if($size -eq 256){0}else{$size}));$bw.Write([byte]0);$bw.Write([byte]0);$bw.Write([UInt16]1);$bw.Write([UInt16]32);$bw.Write([UInt32]$pngs[$i].Length);$bw.Write([UInt32]$offset);$offset+=$pngs[$i].Length};foreach($png in $pngs){$bw.Write($png)};$bw.Dispose();$fs.Dispose()}
Write-Ico (Join-Path $OutputDirectory 'peerchat-client.ico') $false
Write-Ico (Join-Path $OutputDirectory 'peerchat-server.ico') $true
