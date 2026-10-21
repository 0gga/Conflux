pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
Rectangle {
 id:root
 property string text:""
 signal dismissed()
 implicitHeight:body.implicitHeight+24
 radius:ControlStyle.controlRadius
 color:Theme.colors.panel
 border.color:Theme.colors.warning
 clip:true
 RowLayout {
  id:body;anchors.left:parent.left;anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;anchors.margins:12;spacing:12
  Icon {name:"warning";size:18;color:Theme.colors.warning;Layout.alignment:Qt.AlignTop;Layout.topMargin:10}
  Text {Layout.fillWidth:true;Layout.minimumWidth:0;text:root.text;color:Theme.colors.text;font.pixelSize:13;wrapMode:Text.Wrap;maximumLineCount:6;elide:Text.ElideRight;textFormat:Text.PlainText}
  ActionButton {objectName:"dismissNoticeButton";text:"Dismiss";compact:true;Layout.alignment:Qt.AlignVCenter;onClicked:root.dismissed()}
 }
}
