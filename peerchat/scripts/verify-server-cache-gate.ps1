param([Parameter(Mandatory=$true)][string]$Assembly,[switch]$Candidate)
$ErrorActionPreference='Stop'
$stream=[IO.File]::OpenRead((Resolve-Path -LiteralPath $Assembly).Path)
$pe=[System.Reflection.PortableExecutable.PEReader]::new($stream)
try {
 $metadata=[System.Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe)
 $found=$false
 foreach($handle in $metadata.TypeDefinitions){
  $type=$metadata.GetTypeDefinition($handle)
  foreach($fieldHandle in $type.GetFields()){
   $field=$metadata.GetFieldDefinition($fieldHandle);$name=$metadata.GetString($field.Name)
   if($metadata.GetString($type.Name) -eq 'AttachmentCacheRuntime' -and $name -eq 'TestFault'){throw 'Test-only cache fault injector entered package'}
   if($metadata.GetString($type.Name) -eq 'DiscoveryServer' -and $name -eq 'AttachmentCacheTransportReady'){
    $constant=$metadata.GetConstant($field.GetDefaultValue());$bytes=$metadata.GetBlobBytes($constant.Value)
    if($bytes.Count -ne 1 -or ($bytes[0] -ne 0) -ne $Candidate.IsPresent){throw 'Cache candidate gate mismatch'}
    $found=$true
   }
  }
 }
 if(!$found){throw 'Cache gate metadata missing'}
 Write-Output "PASS server cache gate candidate=$($Candidate.IsPresent); no test fault injection: $Assembly"
} finally {$pe.Dispose();$stream.Dispose()}
