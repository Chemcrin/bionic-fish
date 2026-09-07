param([string]$Port='COM7',[int]$Secs=6)
$sp = New-Object System.IO.Ports.SerialPort $Port,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 200
try {
  $sp.Open()
  $deadline = (Get-Date).AddSeconds($Secs)
  $sb = New-Object System.Text.StringBuilder
  while ((Get-Date) -lt $deadline) {
    try {
      $n = $sp.BytesToRead
      if ($n -gt 0) {
        $buf = New-Object byte[] $n
        $r = $sp.Read($buf,0,$n)
        if ($r -gt 0) {
          for ($i=0;$i -lt $r;$i++){
            $c = [char]$buf[$i]
            if ($c -eq 10 -or ($c -ge 32 -and $c -lt 127)) { [void]$sb.Append($c) }
            else { [void]$sb.Append('[' + $buf[$i].ToString('X2') + ']') }
          }
        }
      } else { Start-Sleep -Milliseconds 30 }
    } catch { }
  }
  Write-Output "-----CAPTURED-----"
  Write-Output $sb.ToString()
  Write-Output "-----END-----"
} catch {
  Write-Output ("ERR: " + $_.Exception.Message)
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
