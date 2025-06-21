#!/bin/bash

# Setup polkit permissions for user 'lg' to use systemctl commands

echo "Setting up polkit permissions for user 'lg'..."

# Create polkit rule file
sudo tee /etc/polkit-1/rules.d/49-allow-lg-systemctl.rules > /dev/null << 'EOF'
polkit.addRule(function(action, subject) {
    if (action.id.match("org.freedesktop.systemd1.") &&
        subject.user == "lg") {
        return polkit.Result.YES;
    }
});
EOF

# Restart polkit service to apply changes
sudo systemctl restart polkit

echo "Polkit setup complete!"
echo "User 'lg' can now use systemctl commands without sudo"
echo "Test with: su - lg, then: systemctl status ssh"
