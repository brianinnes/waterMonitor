# Water Monitor - test runtime

This directory contains a vagrant file which runs the backend environment for the water monitor.
This is available for a test environment.

The vagrant file installs a mosquitto broker, with the TLS/SSL configuration and certificates.  The broker authentication is enabled with the credentials:

- username: mosquitto  
- password: passw0rd  

MQTT client will be able to access the broker using port 8883 on the IP Address of the system running the virtual machine.  The CA certificate needed to verify the SSL connection will be available in the **vagrant** directory.  However, the code has comments to enable or disable the server certificate validation, which may not be possible if the host system hostname is not resolvable in DNS.

A NoSQL database, couchDB, is also installed, which is where data is saved to.  The console can be found at [http://127.0.0.1:5984/_utils/](http://127.0.0.1:5984/_utils/)

- username : admin
- password : passw0rd

The Vagrant VM also hosts Node-RED, which implements the back end functionality.

- The editor is available [http://localhost:1880](http://localhost:1880)
- The dashboard is available [http://localhost:1880/ui/#/0](http://localhost:1880/ui/#/0)

## Using the vagrant file

To use the vagrant file you need:

- An up to date installation of [VirtualBox](https://www.virtualbox.org) with the additional Extension pack installed 
- An up to date installation of [Vagrant](https://www.vagrantup.com)  

On a command line in this directory you can use the following commands:
  
- **vagrant up** - Starts the virtual machine (creates it is necessary)
- **vagrant ssh** - ssh into the running virtual machine  
- **vagrant destroy** - stops and deletes the virtual machine (note - certificates will be different next time *vagrant up* is used and the database will be lost)
- **vagrant suspend** - suspends the running virtual machine  
- **vagrant resume** - resume a suspended virtual machine  
- **vagrant halt** - stops a running virtual machine  

## Vagrant setup

You need to add a plugin to Vagrant to work with VirtualBox:

```vagrant plugin install vagrant-vbguest```

you should also ensure that you have all the plugins and boxes upto date, from within the vagrant directory of the cloned git repository issue the following commands:

```bash
vagrant box update
vagrant plugin update
```

## Additional setup for Windows host

Windows can be challenging when running Linux guests due to a number of limitations:

- There is a maximum file path length of 260 characters
- Only Administrators can create symbolic links

For VirtualBox providers there is an additional issue that may present, where Vagrant cannot find Virtual Box.  The following instructions will help overcome these issues:

1. Add the following System Environment variable using control panel:

    ```VBOX_INSTALL_PATH = C:\Program Files\Oracle\VirtualBox\```

2. Update security policy to allow your user to create symbolic links:
   - Launch the security policy tool : **secpol.msc**
   - Navigate to and select **Local Policies -> User Rights Assignment** in the left hand navigation panel
   - Double click **Create symbolic links** in the right panel to open the property editor
   - Click **Add User or Group**
   - Enter your username then press **Check Names**
   - Close the editor by pressing the **OK** button
   - Close the security policy tool

3. Enable long file names (introduced in the anniversary update to Windows 10)
    - Launch **regedit**
    - Navigate to **HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem** in left hand panel
    - Select and double-click **LongPathsEnabled** in the right panel
    - Change the value from 0 to 1 to enable long paths
    - Select **OK** to close the editor
    - Close RegEdit

You need to reboot Windows to make the changes live
