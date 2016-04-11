SRCDIR=$HOME/src/xtree/mailbot

rm -rf ~/b/mailbotx
mkdir -p ~/b/mailbotx
cd ~/b/mailbotx
cmake -G Xcode -DCMAKE_TOOLCHAIN_FILE=$SRCDIR/macports.cmake $SRCDIR

rm -rf ~/b/mailbotb
mkdir -p ~/b/mailbotb
cd ~/b/mailbotb
cmake -DCMAKE_TOOLCHAIN_FILE=$SRCDIR/macports.cmake $SRCDIR
