# gccwrap & clangwrap
### Drop-in preprocessing extensions for GCC and CLang compilers
---


As a C programmer I'm comfortable with C's preprocessor, but I also wanted to have some extensions of the existing preprocessor to make coding less painful.  To do that I created an application called [**cauxp**](http://cauxp.sourceforge.net).

Cauxp has one drawback - it can't be easily integrated into build processes.  Either you create lots if intermediate files and compile them with gcc or clang or some variant on that.  I was not happy with that.  So I wrote gccwrap and clangwrap.

Theses are wrapper applications that lauch gcc or clang but first force gcc and clang to use a wrapper shared library to intercept file open operations so that additional preprocessing can be performed on the source files.  All of this is hidden from the user.  You just replace gcc with gccwrap on a command line and that's it.


gccwrap can take multiple additional preprocessors.  All it requires is that they accept a commanbd line like "cauxp -o &lt;outputfile> &lt;inputfile>".


###How it works.

gccwrap uses the Linux LD_PRELOAD mechanism to force gcc to load a shared library that wraps around the open() calls gcc and it's child applications call.  Any files with standard source file extensions ( in C, C++ or Objective C ) which are not /usr will be intercepted and the gcc applications will get a file descriptor that actually points at the processed version of the file.  The processed versions are in /tmp and should be deleted automatically.

The gccwrap and clangwrap applications and the shared library ( wrap_open.so ) need to be in the same directory, as that's where gccwrap and clangwrap look for the shared library.

So a compilation with gccwrap would look like :

```
gccwrap cauxp -o myapp myapp.c file1.c file2.c
```

The only change is the additional parameter which is the list of commands to apply to input files.

You could have multiple commands to apply :

```
gccwrap cauxp,custom1 -o myapp myapp.c file1.c file2.c
```

In this case each source file ( including header files outside /usr ) will be processed first by cauxp and the result of that processed by custom1 and that result is what gcc itself is fed.

**It's that simple.**


