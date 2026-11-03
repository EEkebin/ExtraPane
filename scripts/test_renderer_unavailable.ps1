param([ValidateSet('D3D12','Vulkan')][string]$RendererName='D3D12')
$ErrorActionPreference='Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
$reg='HKCU:\Software\DM\Receiver';$saved=Get-ItemProperty $reg
$stage=Join-Path (Resolve-Path out) ('renderer-fixture-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item out/winui/SideScreen/* -Destination $stage -Recurse
# Replace only this isolated copy's engine with a synthetic capability report.
Remove-Item -LiteralPath (Join-Path $stage 'dm-view.exe')
Add-Type -Path tests/renderer_probe_fixture.cs -OutputAssembly (Join-Path $stage 'dm-view.exe') -OutputType ConsoleApplication
$app=$null
function Element($name){
 $app.Refresh();$root=[System.Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
 $condition=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,$name)
 $element=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$condition)
 if(!$element){throw "Missing UI element: $name"};return $element
}
try {
 Set-ItemProperty $reg Renderer $RendererName.ToLowerInvariant()
 $app=Start-Process (Join-Path $stage 'SideScreen.exe') -ArgumentList '--client --test-report --test-duration 45' -PassThru
 Start-Sleep -Seconds 2
 (Element 'Settings').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
 Start-Sleep -Seconds 2
 if((Get-ItemProperty $reg).Renderer -ne 'd3d11'){throw 'Unsupported saved preference did not fall back'}
 $null=Element 'D3D12 unavailable: The graphics driver cannot share decoder textures with D3D12.'
 $null=Element 'Vulkan unavailable: Vulkan runtime is missing. Install or update the graphics driver.'
 (Element 'Renderer').GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
 Start-Sleep -Milliseconds 300
 if((Element 'D3D12').Current.IsEnabled){throw 'Unsupported renderer must remain visible but disabled'}
 if((Element 'Vulkan').Current.IsEnabled){throw 'Unsupported Vulkan must remain visible but disabled'}
 (Element 'Renderer').GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Collapse()
 & out/build/windows-release/dm-tray-check.exe $app.Id client
 if($LASTEXITCODE){throw 'Settings app did not exit'}
 $app=Start-Process (Join-Path $stage 'SideScreen.exe') -ArgumentList '--smoke-test --settings' -PassThru
 if(!$app.WaitForExit(15000) -or $app.ExitCode){throw 'Unavailable Settings screenshot failed'}
 Copy-Item (Join-Path $stage 'ui-check.bmp') out/settings-unavailable.bmp
 'UNAVAILABLE_RENDERER passed: visible disabled option, specific explanation, saved preference fallback, normal exit'
}finally{
 if($app -and !$app.HasExited){$app.Kill();$app.WaitForExit()}
 foreach($name in @('Renderer','Page')){if($saved.PSObject.Properties.Name -contains $name){Set-ItemProperty $reg $name $saved.$name}else{Remove-ItemProperty $reg $name -ErrorAction SilentlyContinue}}
 # The generated staging path is constrained to this workspace's out directory.
 $resolved=[IO.Path]::GetFullPath($stage);$root=[IO.Path]::GetFullPath((Join-Path (Get-Location) 'out'))+[IO.Path]::DirectorySeparatorChar
 if(!$resolved.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe fixture cleanup path'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
