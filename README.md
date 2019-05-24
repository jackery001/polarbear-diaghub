# polarbear-diaghub

很鸡肋的提权，需要当前用户密码（当前用户可以为普通用户） 
* 运行如下:  
  polarbear.exe <当前用户名> <密码>

* 执行流程：  
通过<https://github.com/SandboxEscaper/polarbearrepo/tree/master/bearlpe>  
将system32下面的license.rtf进行文件属性覆盖  
当前用户会拥有license.rtf所有权限，然后用FakeDll.dll覆盖license.rtf  
polarbear.exe会调用diaghub.exe ，diaghub再调用FakeDll.dll得到系统权限  

三个文件夹里面是源代码，可以通过vs2017编译。
