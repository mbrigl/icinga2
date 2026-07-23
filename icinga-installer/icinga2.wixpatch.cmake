<CPackWiXPatch>
  <CPackWiXFragment Id="#PRODUCT">
    <Property Id="ALLUSERS">1</Property>
    <Property Id="MSIRESTARTMANAGERCONTROL">Disable</Property>

    <!-- Optional overrides for the data directory and the service name, e.g.
         msiexec /i Icinga2.msi ICINGA_DATA_DIR="D:\IcingaData" ICINGA_SERVICE_NAME=icinga2-b -->
    <Property Id="ICINGA_DATA_DIR" Secure="yes" />
    <Property Id="ICINGA_SERVICE_NAME" Secure="yes" />

    <!-- Instance transforms for installing multiple agent instances on the same host, e.g.
         msiexec /i Icinga2.msi MSINEWINSTANCE=1 TRANSFORMS=:Instance2 INSTALL_ROOT="C:\Program Files\ICINGA2-2"
         Each instance gets its own ProductCode/UpgradeCode so that it shows up, upgrades
         and uninstalls as a separate product. The GUIDs are generated deterministically
         at configure time (see CMakeLists.txt). -->
    <Property Id="INSTANCEID" Value="Default" Secure="yes" />
    <InstanceTransforms Property="INSTANCEID">
      <Instance Id="Instance2" ProductCode="@ICINGA2_INSTANCE2_PRODUCT_GUID@" UpgradeCode="@ICINGA2_INSTANCE2_UPGRADE_GUID@" ProductName="Icinga 2 (Instance 2)" />
      <Instance Id="Instance3" ProductCode="@ICINGA2_INSTANCE3_PRODUCT_GUID@" UpgradeCode="@ICINGA2_INSTANCE3_UPGRADE_GUID@" ProductName="Icinga 2 (Instance 3)" />
      <Instance Id="Instance4" ProductCode="@ICINGA2_INSTANCE4_PRODUCT_GUID@" UpgradeCode="@ICINGA2_INSTANCE4_UPGRADE_GUID@" ProductName="Icinga 2 (Instance 4)" />
      <Instance Id="Instance5" ProductCode="@ICINGA2_INSTANCE5_PRODUCT_GUID@" UpgradeCode="@ICINGA2_INSTANCE5_UPGRADE_GUID@" ProductName="Icinga 2 (Instance 5)" />
    </InstanceTransforms>

    <PropertyRef Id="WIX_IS_NETFRAMEWORK_46_OR_LATER_INSTALLED" />
    <Condition Message='This application requires .NET Framework 4.6 or higher. Please install the .NET Framework then run this installer again.'>
      <![CDATA[Installed OR WIX_IS_NETFRAMEWORK_46_OR_LATER_INSTALLED]]>
    </Condition>

    <CustomAction Id="XtraUpgradeNSIS" BinaryKey="icinga2_installer" ExeCommand="upgrade-nsis" Execute="deferred" Impersonate="no" />
    <!-- The trailing space inside the quotes keeps a trailing backslash in the property
         value from escaping the closing quote; the installer trims it again. -->
    <CustomAction Id="XtraInstall" FileKey="CM_FP_sbin.icinga2_installer.exe" ExeCommand='install "[ICINGA_DATA_DIR] " "[ICINGA_SERVICE_NAME] " "[INSTANCEID] "' Execute="deferred" Impersonate="no" />
    <CustomAction Id="XtraUninstall" FileKey="CM_FP_sbin.icinga2_installer.exe" ExeCommand='uninstall "[INSTANCEID] "' Execute="deferred" Impersonate="no" />

    <Binary Id="icinga2_installer" SourceFile="$<TARGET_FILE:icinga-installer>" />

    <InstallExecuteSequence>
      <Custom Action="XtraUpgradeNSIS" After="InstallInitialize">$CM_CP_sbin.icinga2_installer.exe&gt;2 AND NOT SUPPRESS_XTRA</Custom>
      <Custom Action="XtraInstall" Before="InstallFinalize">$CM_CP_sbin.icinga2_installer.exe&gt;2 AND NOT SUPPRESS_XTRA</Custom>
      <Custom Action="XtraUninstall" Before="RemoveExistingProducts">$CM_CP_sbin.icinga2_installer.exe=2 AND NOT SUPPRESS_XTRA</Custom>
    </InstallExecuteSequence>

    <!--
      Write the path to eventprovider.dll to the registry so that the Event Viewer is able to find
      the message definitions and properly displays our log messages.

      See also: https://docs.microsoft.com/en-us/windows/win32/eventlog/reporting-an-event
    -->
    <FeatureRef Id="ProductFeature" IgnoreParent="yes">
      <Component Id="EventProviderRegistryEntry" Guid="*" Directory="INSTALL_ROOT">
        <RegistryKey Root="HKLM" Key="SYSTEM\CurrentControlSet\Services\EventLog\Application\Icinga 2" Action="createAndRemoveOnUninstall">
          <RegistryValue Name="EventMessageFile" Type="string" Value="[#CM_FP_sbin.eventprovider.dll]" />
        </RegistryKey>
      </Component>
    </FeatureRef>

    <Property Id="WIXUI_EXITDIALOGOPTIONALCHECKBOXTEXT" Value="Run Icinga 2 setup wizard" />

    <Property Id="WixShellExecTarget" Value="[#CM_FP_sbin.Icinga2SetupAgent.exe]" />
    <CustomAction Id="LaunchIcinga2Wizard"
        BinaryKey="WixCA"
        DllEntry="WixShellExec"
        Impersonate="no" />

    <UI>
        <Publish Dialog="ExitDialog"
            Control="Finish"
            Event="DoAction"
            Value="LaunchIcinga2Wizard">WIXUI_EXITDIALOGOPTIONALCHECKBOX = 1 and NOT Installed</Publish>
    </UI>
  </CPackWiXFragment>
</CPackWiXPatch>
