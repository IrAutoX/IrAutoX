$ErrorActionPreference = "Stop"
$root = (Resolve-Path "deploy").Path
$files = Get-ChildItem $root -File -Recurse | Sort-Object FullName
if ($files.Count -eq 0) { throw "Qt deployment is empty" }
$header = @("#pragma once","struct EmbeddedFile { unsigned short id; const wchar_t* path; };","static const EmbeddedFile embeddedFiles[] = {")
$rc = @()
$id = 200
foreach ($file in $files) {
  $relative = $file.FullName.Substring($root.Length + 1).Replace("\","/")
  $safe = $file.FullName.Replace("\","\\")
  $header += ('{ ' + $id + ', L"' + $relative + '" },')
  $rc += ($id.ToString() + ' RCDATA "' + $safe + '"')
  $id++
}
$header += "};"
Set-Content -Path "launcher/embedded_resources.h" -Value $header -Encoding ascii
Set-Content -Path "launcher/embedded_resources.rc" -Value $rc -Encoding ascii
