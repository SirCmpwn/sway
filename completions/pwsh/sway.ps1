# Based on the following documentation and tons of trial and error:
# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/register-argumentcompleter?view=powershell-7.1

Register-ArgumentCompleter -Native -CommandName sway -ScriptBlock {
  param($wordToComplete, $commandAst, $cursorPosition)

  $completions = @(
    '--help',
    '--config',
    '--validate',
    '--debug',
    '--version',
    '--verbose',
    '--get-socketpath'
  )

  if ($commandAst.CommandElements.Count -ge 2) {
    if ($commandAst.CommandElements[1].ToString() -in @( '-c', '--config' )) {
      $completions = Get-ChildItem | Select-Object -ExpandProperty Name
    }
  }

  $completions | Where-Object { $_.StartsWith($wordToComplete) } | ForEach-Object {
    [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
  }
}
