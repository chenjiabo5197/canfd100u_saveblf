# canfd100u_saveblf

#### 介绍
读取zlg canfd100u的发送的can和canfd信号，并保存成blf文件


#### 使用说明
 1、 使用make命令编译，然后执行编译生成的文件即可读取can1接口的信号，并将can信号保存为can.blf文件，canfd信号保存为 canfd.blf文件

 2、编译时出现无libusbcanfd.so或libbinlog.so提示，则使用export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/path/to/library来将本目录添加到so的搜索路径下，然后执行ldconfig命令来更新共享库的缓存即可。使用ldd saveblf可查看编译后的可执行文件依赖的so文件

#### 参与贡献

1.  Fork 本仓库
2.  新建 Feat_xxx 分支
3.  提交代码
4.  新建 Pull Request


#### 特技

1.  使用 Readme\_XXX.md 来支持不同的语言，例如 Readme\_en.md, Readme\_zh.md
2.  Gitee 官方博客 [blog.gitee.com](https://blog.gitee.com)
3.  你可以 [https://gitee.com/explore](https://gitee.com/explore) 这个地址来了解 Gitee 上的优秀开源项目
4.  [GVP](https://gitee.com/gvp) 全称是 Gitee 最有价值开源项目，是综合评定出的优秀开源项目
5.  Gitee 官方提供的使用手册 [https://gitee.com/help](https://gitee.com/help)
6.  Gitee 封面人物是一档用来展示 Gitee 会员风采的栏目 [https://gitee.com/gitee-stars/](https://gitee.com/gitee-stars/)
