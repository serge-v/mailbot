SRCDIR=$HOME/src/xtree/mailbot

mkdir -p ~/b/mailbotx
cd ~/b/mailbotx
cmake -G Xcode -DCMAKE_TOOLCHAIN_FILE=$SRCDIR/macports.cmake $SRCDIR

mkdir -p ~/b/mailbotb
cd ~/b/mailbotb
cmake -DCMAKE_TOOLCHAIN_FILE=$SRCDIR/macports.cmake $SRCDIR
