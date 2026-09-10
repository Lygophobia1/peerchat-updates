param([string]$InstallDirectory='build\native\install')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $cmake='build\tools\cmake-python\cmake\data\bin\cmake.exe'
    if(-not (Test-Path $cmake)) { python -m pip install --target build\tools\cmake-python cmake==4.1.0; if($LASTEXITCODE){throw 'CMake download failed'} }
    function Fetch-Pinned([string]$Path,[string]$Url,[string]$Revision) {
        if(Test-Path "$Path\.git") { $actual=(& git -C $Path rev-parse HEAD).Trim();if($actual -ne $Revision){throw "$Path is not the pinned revision"};return }
        & git init $Path;if($LASTEXITCODE){throw "Could not initialize $Path"}
        & git -C $Path remote add origin $Url
        & git -C $Path fetch --depth 1 origin $Revision;if($LASTEXITCODE){throw "Could not fetch $Path"}
        & git -C $Path checkout --detach FETCH_HEAD;if($LASTEXITCODE){throw "Could not check out $Path"}
    }
    Fetch-Pinned 'build\deps\opus' 'https://github.com/xiph/opus.git' 'ddbe48383984d56acd9e1ab6a090c54ca6b735a6'
    Fetch-Pinned 'build\deps\libdatachannel' 'https://github.com/paullouisageneau/libdatachannel.git' 'c6696d157b5612df2a741d9a03b192b47ab6cefb'
    Fetch-Pinned 'build\deps\libdatachannel\deps\plog' 'https://github.com/SergiusTheBest/plog.git' '94899e0b926ac1b0f4750bfbd495167b4a6ae9ef'
    Fetch-Pinned 'build\deps\libdatachannel\deps\usrsctp' 'https://github.com/paullouisageneau/usrsctp.git' 'fec583d54493f879d2ae44a743423bf8a04371ab'
    Fetch-Pinned 'build\deps\libdatachannel\deps\libjuice' 'https://github.com/paullouisageneau/libjuice.git' '22145ec3473c73d811417ac9a6aee0de530aaa3f'
    Fetch-Pinned 'build\deps\libdatachannel\deps\json' 'https://github.com/nlohmann/json.git' '55f93686c01528224f448c19128836e7df245f72'
    Fetch-Pinned 'build\deps\libdatachannel\deps\libsrtp' 'https://github.com/cisco/libsrtp.git' '24b3bf8f19b6f5ab4cd2bcceb4f4064efca86fd5'
    Fetch-Pinned 'build\deps\mbedtls' 'https://github.com/Mbed-TLS/mbedtls.git' '068ff080b369adfac81509f9b57b2afabaf82dc5'
    Fetch-Pinned 'build\deps\mbedtls\framework' 'https://github.com/Mbed-TLS/mbedtls-framework.git' 'dde0c4a0e448a0552f18817dcea633bb851fd288'
    python build\deps\mbedtls\scripts\config.py set MBEDTLS_SSL_DTLS_SRTP;if($LASTEXITCODE){throw 'Could not enable DTLS-SRTP'}
    $prefix=(Join-Path $projectRoot $InstallDirectory).Replace('\','/')
    & $cmake -S build\deps\mbedtls -B build\native\mbedtls -G 'MinGW Makefiles' -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix"
    & $cmake --build build\native\mbedtls --target install --parallel 4;if($LASTEXITCODE){throw 'Mbed TLS build failed'}
    & $cmake -S build\deps\opus -B build\native\opus -G 'MinGW Makefiles' -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_SHARED_LIBRARY=OFF -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix"
    & $cmake --build build\native\opus --target install --parallel 4;if($LASTEXITCODE){throw 'Opus build failed'}
    & $cmake -S build\deps\libdatachannel -B build\native\libdatachannel -G 'MinGW Makefiles' -DBUILD_SHARED_LIBS=OFF -DUSE_MBEDTLS=ON -DNO_MEDIA=OFF -DNO_WEBSOCKET=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON -DENABLE_WARNINGS_AS_ERRORS=OFF -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$prefix" "-DCMAKE_INSTALL_PREFIX=$prefix"
    & $cmake --build build\native\libdatachannel --target install --parallel 4;if($LASTEXITCODE){throw 'libdatachannel build failed'}
} finally { Pop-Location }
