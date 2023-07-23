#!/bin/bash
wget "https://dl.walletbuilders.com/download?customer=7b1f96875e8d920a06993fdc40190ec9ec9e6193d481002ffe&filename=blocx-qt-linux.tar.gz" -O blocx-qt-linux.tar.gz

mkdir $HOME/Desktop/BLOCX

tar -xzvf blocx-qt-linux.tar.gz --directory $HOME/Desktop/BLOCX

mkdir $HOME/.blocx

cat << EOF > $HOME/.blocx/blocx.conf
rpcuser=rpc_blocx
rpcpassword=dR2oBQ3K1zYMZQtJFZeAerhWxaJ5Lqeq9J2
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
listen=1
server=1
addnode=node2.walletbuilders.com
EOF

cat << EOF > $HOME/Desktop/BLOCX/start_wallet.sh
#!/bin/bash
SCRIPT_PATH=\`pwd\`;
cd \$SCRIPT_PATH
./blocx-qt
EOF

chmod +x $HOME/Desktop/BLOCX/start_wallet.sh

cat << EOF > $HOME/Desktop/BLOCX/mine.sh
#!/bin/bash
SCRIPT_PATH=\`pwd\`;
cd \$SCRIPT_PATH
echo Press [CTRL+C] to stop mining.
while :
do
./blocx-cli generatetoaddress 1 \$(./blocx-cli getnewaddress)
done
EOF

chmod +x $HOME/Desktop/BLOCX/mine.sh

exec $HOME/Desktop/BLOCX/blocx-qt &

sleep 15

cd $HOME/Desktop/BLOCX/

clear

exec $HOME/Desktop/BLOCX/mine.sh