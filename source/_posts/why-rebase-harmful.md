---
title:      "翻译：为什么Rebase是有害的"
date:       2014-08-11 23:06:54
tags:
- git
- 翻译
---

> 只能对private的分支使用rebase，千万不要对public的分支使用rebase，否则你就会明白为什么说rebase是有害的。

原文地址：[Rebase Considered Harmful](http://changelog.complete.org/archives/586-rebase-considered-harmful)

<!--more-->

## Merge的两种方式 ##

1. 向上游提交“干净”的patch。不包含历史信息——日常提交、BUG fix、与上游的Merge记录等。就如同一系列针对当前HEAD的diff。
2. 携带完整的历史，并永久保留。

git鼓励第1种方式——使用rebase。而有些系统则鼓励第2种，例如Darcs。

## git rebase ##

git rebase的工作方式：
1. 从目标仓库中移除所有本地patch；
2. 升级到上游的最新版本；
3. 重新向HEAD提交你的每个本地change——如果有冲突则中止，直到你fix掉。

而传统的Merge则与之相反：
1. 将上游的新patch更新到你的本地分支上；
2. 解掉冲突后提交为新的版本。

## rebase的问题 ##

git rebase的manpage上说：**“当你rebase一个分支时，你是在更改它的历史。如果有人已经保存了这个分支的一份拷贝，当他尝试从你那获取更新时，就可能有问题。”**

为什么会出现问题？因为pull和rebase完全不同，pull是从上游获取一系列commit历史，而rebase则移除了旧的历史。

## 结论 ##

只能对private的分支使用rebase，千万不要对public的分支使用rebase，否则你就会明白为什么说rebase是有害的。