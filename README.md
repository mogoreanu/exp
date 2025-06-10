# exp

The repository contains experimental tools and libraries.

## Environment expectations
* Designed to be built using bazel
* Uses the ABSL libraries
* Optimized for use with vscode

## Getting started

Instructions verified on debian 12

Install bazel and git
```
sudo apt install bazel git
```

```
git clone https://github.com/mogoreanu/exp.git
cd exp
bazel run :my_hello
```