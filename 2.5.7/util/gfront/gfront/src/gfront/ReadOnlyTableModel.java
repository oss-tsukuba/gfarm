package gfront;
import javax.swing.table.DefaultTableModel;

public class ReadOnlyTableModel extends DefaultTableModel {
	public boolean isCellEditable(int row, int column) {
		return false;
	}
}
