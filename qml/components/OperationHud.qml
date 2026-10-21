pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
Item {
 id: root
 property real availableWidth:1040
 property real availableHeight:620
 required property var content
 required property var running
 signal instanceRequested(string path)
 readonly property var history: content.operations
 implicitWidth:buttons.implicitWidth;implicitHeight:buttons.implicitHeight
 RowLayout { id:buttons;anchors.fill:parent;spacing:8
 PopupButton {
  id:tasksButton;objectName:"operationHudButton";popup:tasks
  iconName:root.history.failedCount ? "warning" : root.history.activeCount ? "download" : "check"
  text:root.history.failedCount ? root.history.failedCount+" need attention" : root.history.activeCount ? root.history.activeCount+" active" : root.content.busy ? "Checking" : "Tasks"
  compact:true;pill:true;accented:root.history.failedCount>0;hint:"Content operations"
 }
 PopupButton {
  id:runningButton;objectName:"runningInstancesButton";popup:instances
  iconName:root.running.error ? "warning" : "play"
  text:root.running.error ? "Status unavailable" : root.running.instances.length ? root.running.instances.length+" running" : "No instances running"
  compact:true;pill:true;hint:"Running instances"
 }
 }
 FocusPopup {
  id:tasks;objectName:"operationTasksPopup";parent:tasksButton;x:tasksButton.width-width;y:tasksButton.height+8
  width:Math.min(480,root.availableWidth-32);padding:14
  background:Rectangle{radius: ControlStyle.panelRadius;color:Theme.colors.panel;border.color:Theme.colors.border}
  contentItem:ColumnLayout {
   spacing:12
   Text {text:"Tasks";color:Theme.colors.text;font.pixelSize:20;font.weight:Font.DemiBold}
   Text {visible:root.history.entries.length===0;text:root.content.busy ? "Checking content" : "No active tasks";color:Theme.colors.muted;font.pixelSize:13}
   ContentListView {
    id:taskList;objectName:"operationTaskList";Layout.fillWidth:true;outsideColor:Theme.colors.panel
    Layout.preferredHeight:Math.min(contentHeight+2,Math.max(100,root.availableHeight-150))
    model:root.history.model;spacing:8
    delegate:Rectangle {
     id:task
     required property var modelData
     width:taskList.viewportWidth;implicitHeight:body.implicitHeight+24;radius: ControlStyle.panelRadius;color:Theme.colors.card
     ColumnLayout {
      id:body;anchors.fill:parent;anchors.margins:12;spacing:8
      RowLayout {
       Layout.fillWidth:true
       BusyIndicator {visible:task.modelData.state==="running";running:visible;Layout.preferredWidth:20;Layout.preferredHeight:20}
       Icon {visible:task.modelData.state!=="running";name:task.modelData.state==="failed"?"warning":task.modelData.state==="done"?"check":"download";color:task.modelData.state==="failed"?Theme.colors.warning:Theme.colors.muted;size:18}
       Text {Layout.fillWidth:true;text:task.modelData.pack || "Content";color:Theme.colors.text;font.pixelSize:14;font.weight:Font.DemiBold;elide:Text.ElideRight;textFormat:Text.PlainText}
       ActionButton {objectName:"dismissOperation";visible:task.modelData.state==="failed"||task.modelData.state==="done";iconName:"close";hint:"Dismiss";subtle:true;compact:true;onClicked:root.history.dismiss(task.modelData.id)}
      }
      Text {Layout.fillWidth:true;text:task.modelData.title;elide:Text.ElideRight;textFormat:Text.PlainText;color:Theme.colors.text;font.pixelSize:13}
      Text {Layout.fillWidth:true;text:task.modelData.state==="queued"?"Queued":task.modelData.action;color:Theme.colors.muted;font.pixelSize:12;textFormat:Text.PlainText}
      Text {Layout.fillWidth:true;visible:!!task.modelData.message;text:task.modelData.message;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:task.modelData.state==="failed"?Theme.colors.danger:Theme.colors.muted;font.pixelSize:12}
      ActionButton { text:"Cancel";iconName:"close";subtle:true;compact:true;visible:task.modelData.state==="queued" || (task.modelData.state==="running" && root.content.cancellable);onClicked:root.content.cancelOperation(task.modelData.id) }
      RowLayout {
       visible:task.modelData.state==="failed"
       ActionButton {objectName:"copyOperationDetails";text:"Copy details";iconName:"copy";compact:true;onClicked:root.content.copyOperationDetails(task.modelData.id)}
       ActionButton {objectName:"retryOperation";text:"Retry";iconName:"refresh";primary:true;compact:true;visible:!!task.modelData.retryable;onClicked:root.history.retry(task.modelData.id)}
      }
     }
    }
   }
  }
 }
 FocusPopup {
  id:instances;objectName:"runningInstancesPopup";parent:runningButton;x:runningButton.width-width;y:runningButton.height+8;width:300;padding:14
  background:Rectangle{radius: ControlStyle.panelRadius;color:Theme.colors.panel;border.color:Theme.colors.border}
  contentItem:ColumnLayout {
   spacing:10
   RowLayout {
    Layout.fillWidth:true
    Text {Layout.fillWidth:true;text:"Instances";color:Theme.colors.text;font.pixelSize:20;font.weight:Font.DemiBold}
    ActionButton {iconName:"refresh";hint:"Refresh status";subtle:true;compact:true;onClicked:root.running.refresh()}
   }
   Text {Layout.fillWidth:true;visible:!!root.running.error;text:root.running.error;color:Theme.colors.warning;wrapMode:Text.WordWrap;font.pixelSize:12}
   Text {visible:!root.running.error && root.running.instances.length===0;text:"No instances running";color:Theme.colors.muted;font.pixelSize:13}
   ContentListView {
    id:instanceList;Layout.fillWidth:true;visible:root.running.instances.length>0;outsideColor:Theme.colors.panel
    Layout.preferredHeight:Math.min(contentHeight+2,Math.max(80,root.availableHeight-160))
    model:root.running.instances
    delegate:ActionButton {required property var modelData;width:instanceList.viewportWidth;text:modelData.title || modelData.name || "Minecraft";iconName:"play";alignLeft:true;onClicked:{instances.close();root.instanceRequested(modelData.path)}}
   }
  }
 }
}
