param(
  [string]$Port='COM7',
  [string]$Command='',
  [int]$Baud=115200,
  [int]$WaitMs=600
)
$sp = New-Object System.IO.Ports.SerialPort $Port,$Baud,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$sp.Handshake = [System.IO.Ports.Handshake]::None
$sp.ReadTimeout = 300
try {
  $sp.Open()
  # 先清空缓冲区里的旧数据
  Start-Sleep -Milliseconds 80
  while ($sp.BytesToRead -gt 0) { [void]$sp.ReadByte() }

  if ($Command -ne '') {
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Command + "`r`n")
    $sp.Write($bytes,0,$bytes.Length)
  }

  $deadline = (Get-Date).AddMilliseconds($WaitMs)
  $sb = New-Object System.Text.StringBuilder
  while ((Get-Date) -lt $deadline) {
    if ($sp.BytesToRead -gt 0) {
      $n = $sp.BytesToRead
      $buf = New-Object byte[] $n
      $r = $sp.Read($buf,0,$n)
      for ($i=0;$i -lt $r;$i++){
        $c = [char]$buf[$i]
        if ($c -eq 10 -or $c -eq 13 -or ($c -ge 32 -and $c -lt 127)) { [void]$sb.Append($c) }
        else { [void]$sb.Append('<' + $buf[$i].ToString('X2') + '>') }
      }
    } else { Start-Sleep -Milliseconds 40 }
  }
  Write-Output ("[TX] " + $Command)
  Write-Output ("[RX] " + $sb.ToString())
} catch {
  Write-Output ("ERR: " + $_.Exception.Message)
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
