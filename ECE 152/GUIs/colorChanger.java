import javax.swing.*;
import java.awt.*;
import java.awt.event.*;

public class colorChanger implements ActionListener {
	JFrame frame;
  
	public static void main(String args[]) {
		colorChanger gui = new colorChanger();
		gui.go();
    }
	
	public void go(){
		frame = new JFrame();
		frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
		
		JButton button = new JButton("Change Colors");
		button.addActionListener(this);
		MyDrawPanel drawPanel = new MyDrawPanel();
		
		frame.getContentPane().add(BorderLayout.SOUTH, button);
		frame.getContentPane().add(BorderLayout.CENTER, drawPanel);
		frame.setSize(500, 500);
		frame.setVisible(true);
	}
	
	public void actionPerformed(ActionEvent event){
		frame.repaint();
	}
}

class MyDrawPanel extends JPanel {
	public void paintComponent(Graphics g){
		//fill the oval with a random color
		Graphics2D g2d = (Graphics2D) g;
		int red = (int) (Math.random() * 256);
		int green = (int) (Math.random() * 256);
		int blue = (int) (Math.random() * 256);
		Color startColor = new Color(red, green, blue);
		
		red = (int) (Math.random() * 256);
		green = (int) (Math.random() * 256);
		blue = (int) (Math.random() * 256);
		Color endColor = new Color(red, green, blue);
		
		GradientPaint gradient = new GradientPaint(70,70,startColor,150,150,endColor);
		g2d.setPaint(gradient);
		g2d.fillOval(70,70,100,100);
	}
}