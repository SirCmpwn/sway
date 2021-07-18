# Based on the following documentation and tons of trial and error:
# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/register-argumentcompleter?view=powershell-7.1

Register-ArgumentCompleter -Native -CommandName swaynag -ScriptBlock {
  param($wordToComplete, $commandAst, $cursorPosition)

  $completions = @(
    '--button',
    '--button-no-terminal',
    '--button-dismiss',
    '--button-dismiss-no-terminal',
    '--config',
    '--debug',
    '--edge',
    '--font',
    '--help',
    '--detailed-message',
    '--detailed-button',
    '--message',
    '--output',
    '--dismiss-button',
    '--type',
    '--version',

    # Appearance
    '--background',
    '--border',
    '--border-bottom',
    '--button-background',
    '--text',
    '--border-bottom-size',
    '--message-padding',
    '--details-border-size',
    '--button-border-size',
    '--button-gap',
    '--button-dismiss-gap',
    '--button-margin-right',
    '--button-padding'
  )

  if ($commandAst.CommandElements.Count -ge 2) {
    if ($commandAst.CommandElements[1].ToString() -in @( '-c', '--config' )) {
      $completions = Get-ChildItem | Select-Object -ExpandProperty Name
    } elseif ($commandAst.CommandElements[1].ToString() -in @( '-e', '--edge' )) {
      $completions = @(
        'top',
        'bottom'
      )
    } elseif ($commandAst.CommandElements[1].ToString() -in @( '-t', '--type' )) {
      $completions = @(
        'error',
        'warning'
      )
    }
  }

  $completions | Where-Object { $_.StartsWith($wordToComplete) } | ForEach-Object {
    [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
  }
}
