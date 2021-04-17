**What is it?**

xearth with wayland support. See the original README file for author and license details.

**Why?**

It was a somewhat reasonable excuse to try out some wayland programming.

**Can I actually use it as my background?**

Probably not, it would need support for something like wlr_layer_shell.

**There are no window controls!**

It relies on server side decorations, which does not work in for
example gnome.

**How do I build it?**

You need to install some development headers for at least wayland, gd
and cairo, and then

```bash
cp Makefile.DIST Makefile
HAVE_WAYLAND=1 make
```

It will probably only compile on linux (uses of timerfd)
