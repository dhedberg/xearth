**What is it?**

xearth with wayland support. See the original README file for author and license details.

**Why?**

It was a somewhat reasonable excuse to try out some wayland programming.

**Can I actually use it as my background?**

Yes, if your compositor supports the unstable
[wlr-layer-shell](https://wayland.app/protocols/wlr-layer-shell-unstable-v1)
protocol you should be able to render to the background by passing `-root`,

```bash
./xearth -root
```

You may need to get rid of any other process already rendering to the
background, such as swaybg.

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

The included Dockerfiles contains the necessary dependencies and steps
for ubuntu and fedora.

It will probably only compile on linux (uses timerfd).
