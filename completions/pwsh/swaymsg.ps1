# Based on the following documentation and tons of trial and error:
# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/register-argumentcompleter?view=powershell-7.1

Register-ArgumentCompleter -Native -CommandName swaymsg -ScriptBlock {
  param($wordToComplete, $commandAst, $cursorPosition)

  $completions = @(
    '--help',
    '--monitor',
    '--pretty',
    '--quiet',
    '--raw',
    '--socket',
    '--type',
    '--version'
  )

  if ($commandAst.CommandElements.Count -ge 2) {
    if ($commandAst.CommandElements[1].ToString() -in @( '-t', '--type' )) {
      $completions = @(
        'get_workspaces',
        'get_inputs',
        'get_outputs',
        'get_tree',
        'get_marks',
        'get_bar_config',
        'get_version',
        'get_binding_modes',
        'get_binding_state',
        'get_config',
        'get_seats',
        'send_tick',
        'subscribe'
      )
    } elseif ($commandAst.CommandElements[1].ToString() -in @( '-s', '--socket' )) {
      $completions = Get-ChildItem -Path '/run/user/*/sway-ipc*' | Select-Object -ExpandProperty FullName
    }
  }

  $completions | Where-Object { $_.StartsWith($wordToComplete) } | ForEach-Object {
    [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
  }
}
