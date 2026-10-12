# Runs a command and kills its WHOLE process tree if their combined memory passes -MaxGB.
# (The 'python' on PATH is a launcher stub that starts the real interpreter as a child, so
#  watching only the started process sees ~13 MB while the child grows unbounded.)
#   powershell -File capped.ps1 -MaxGB 6 -Log out.txt -Exe python -ArgLine "native30.py --source ..."
param([double]$MaxGB = 6, [string]$Log = "$env:TEMP\capped.log",
      [Parameter(Mandatory)][string]$Exe, [string]$ArgLine = '')
$p = Start-Process -FilePath $Exe -ArgumentList $ArgLine -NoNewWindow -PassThru `
     -RedirectStandardOutput $Log -RedirectStandardError "$Log.err"

function Tree($root) {
    $all = Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, WorkingSetSize
    $ids = @($root); $grew = $true
    while ($grew) {
        $kids = $all | Where-Object { $ids -contains $_.ParentProcessId -and $ids -notcontains $_.ProcessId }
        $grew = [bool]$kids; $ids += $kids.ProcessId
    }
    $all | Where-Object { $ids -contains $_.ProcessId }
}

$peak = 0
while (-not $p.HasExited) {
    Start-Sleep -Milliseconds 500
    $t = Tree $p.Id
    $mb = ($t | Measure-Object WorkingSetSize -Sum).Sum / 1MB
    if ($mb -gt $peak) { $peak = $mb }
    if ($mb -gt $MaxGB * 1024) {
        $t | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        "KILLED at $([int]$mb) MB (cap $MaxGB GB)"; break
    }
}
"exit $($p.ExitCode), peak $([int]$peak) MB (whole tree)"
