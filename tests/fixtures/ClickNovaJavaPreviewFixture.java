// SPDX-License-Identifier: MIT
// Disposable interactive fixture: no files, network, or persistence.
import javax.swing.*;
import java.awt.*;
public final class ClickNovaJavaPreviewFixture {
    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> {
            JFrame frame = new JFrame("ClickNovaPhase22JavaFixture");
            frame.setDefaultCloseOperation(WindowConstants.EXIT_ON_CLOSE);
            JPanel content = new JPanel(new FlowLayout());
            content.add(new JButton("Disposable button"));
            content.add(new JLabel("Disposable label"));
            content.add(new JPasswordField(9));
            frame.setContentPane(content);
            frame.setSize(440, 180);
            frame.setLocation(100, 100);
            frame.setVisible(true);
        });
    }
}
