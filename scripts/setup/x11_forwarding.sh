#!/bin/bash

# X11 setup script for Raspberry Pi (MobaXterm compatibility)
echo "Setting up X11 forwarding for MobaXterm..."

# Update package list
sudo apt update

# Install X11 packages
sudo apt install -y xorg xserver-xorg-core xserver-xorg-input-all xauth x11-apps

# Backup original sshd_config
sudo cp /etc/ssh/sshd_config /etc/ssh/sshd_config.backup

# Configure SSH for X11 forwarding
sudo tee -a /etc/ssh/sshd_config > /dev/null << EOF

# X11 Forwarding Configuration
X11Forwarding yes
X11DisplayOffset 10
X11UseLocalhost no
EOF

# Restart SSH service
sudo systemctl restart ssh

echo "X11 setup complete!"
echo "Now connect from MobaXterm with X11 forwarding enabled"
echo "Test with: xeyes or xclock"
