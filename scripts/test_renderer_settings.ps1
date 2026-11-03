# Live UI/session test. Requires the installed driver and an idle desktop.
# Restores the user's renderer/address/page preferences when it finishes.
param([ValidateSet('D3D12','Vulkan','OpenGL')][string]$RendererName='D3D12')
$ErrorActionPreference='Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
$reg='HKCU:\Software\DM\Receiver';$saved=Get-ItemProperty $reg
$app=$null;$hostProcess=$null
function Root {$app.Refresh();[System.Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)}
function Element($name) {
 $condition=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,$name)
 $element=(Root).FindFirst([System.Windows.Automation.TreeScope]::Descendants,$condition)
 if(!$element){throw "UI element missing: $name"};return $element
}
function Invoke($name){(Element $name).GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke();Start-Sleep -Milliseconds 400}
function Nav($name){$element=Element $name;$patterns=$element.GetSupportedPatterns();if($patterns -contains [System.Windows.Automation.SelectionItemPattern]::Pattern){$element.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()}else{$element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()};Start-Sleep -Seconds 1}
function Renderer($name){$combo=Element 'Renderer';$combo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand();Start-Sleep -Milliseconds 300;(Element $name).GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select();Start-Sleep -Milliseconds 400}
function Wait-Log($pattern){for($i=0;$i -lt 150;$i++){Start-Sleep -Milliseconds 200;$text=Get-Content out/winui/SideScreen/client-session-check.log -Raw -ErrorAction SilentlyContinue;if($text -match $pattern){return $text}};throw "Missing $pattern : $text"}
try {
 Set-ItemProperty $reg Peer '127.0.0.1';Set-ItemProperty $reg Renderer 'd3d11'
 $app=Start-Process out/winui/SideScreen/SideScreen.exe -ArgumentList '--client --test-report --test-duration 180' -PassThru;Start-Sleep -Seconds 2
 Nav 'Settings';Renderer $RendererName
 if((Get-ItemProperty $reg).Renderer -ne $RendererName.ToLowerInvariant()){throw 'Renderer not saved'}
 & out/build/windows-release/dm-tray-check.exe $app.Id client
 if($LASTEXITCODE){throw 'Idle Settings close failed'}
 $hostProcess=Start-Process out/build/windows-release/dm-view.exe -ArgumentList 'host --lan --bind 127.0.0.1 --allow-screen --allow-input --extend --serve' -RedirectStandardOutput out/settings-host.log -RedirectStandardError out/settings-host.err -PassThru
 Start-Sleep -Seconds 1
 Remove-Item -LiteralPath out/winui/SideScreen/client-session-check.log -ErrorAction SilentlyContinue
 $app=Start-Process out/winui/SideScreen/SideScreen.exe -ArgumentList '--client --start --test-report --test-duration 180' -PassThru
 $null=Wait-Log "RENDERER_CHANGED $RendererName"
 Nav 'Settings'
 $selected=(Element 'Renderer').GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()
 if($selected.Count -ne 1 -or $selected[0].Current.Name -ne $RendererName){throw 'Saved renderer selection not restored in Settings'}
 if((Element 'Renderer').Current.IsEnabled){throw 'Renderer should be locked during session'}
 if((Element 'Send mouse, keyboard, touch and pen').Current.IsEnabled){throw 'Client options should be locked'}
 if(!(Element 'Allow clients to control mouse, keyboard, touch and pen').Current.IsEnabled){throw 'Idle server options should remain editable'}
 Nav 'Client';Invoke 'Disconnect';Start-Sleep -Seconds 1
 Nav 'Settings';Renderer 'D3D11 (default)';Nav 'Client'
 Remove-Item -LiteralPath out/winui/SideScreen/client-session-check.log -ErrorAction SilentlyContinue
 Invoke 'Connect';$null=Wait-Log 'PREVIEW_RENDERER api=D3D11'
 & out/build/windows-release/dm-tray-check.exe $app.Id client
 if($LASTEXITCODE){throw 'Active client close failed'}
 "SETTINGS passed: shared options, hardware probe, saved $RendererName across restart, active-session lock, D3D11 next connection, normal close"
}finally{
 if($app -and !$app.HasExited){$app.Kill();$app.WaitForExit()}
 if($hostProcess -and !$hostProcess.HasExited){$hostProcess.Kill();$hostProcess.WaitForExit()}
 foreach($name in @('Peer','Renderer','Page')){if($saved.PSObject.Properties.Name -contains $name){Set-ItemProperty $reg $name $saved.$name}else{Remove-ItemProperty $reg $name -ErrorAction SilentlyContinue}}
}
