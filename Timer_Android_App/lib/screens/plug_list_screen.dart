import 'package:flutter/material.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/services.dart';
import 'package:timer_app/screens/plug_control_screen.dart';

class PlugListScreen extends StatefulWidget {
  const PlugListScreen({super.key});

  @override
  State<PlugListScreen> createState() => _PlugListScreenState();
}

class _PlugListScreenState extends State<PlugListScreen> {
  late final String uid;
  final plugsRef = FirebaseDatabase.instance.ref();

  @override
  void initState() {
    super.initState();
    uid = FirebaseAuth.instance.currentUser!.uid;
  }

  Future<void> addDummyPlug() async {
    final plugId = "plug${DateTime.now().millisecondsSinceEpoch}";
    await plugsRef.child("users/$uid/plugs/$plugId").set({
      'name': "Plug created at ${DateTime.now().hour}:${DateTime.now().minute}",
      'isOn': false,
      'isPaused': false,
      'durationMinutes': 15,
      'timeRemaining': 15,
      'isDeleted': false,
      'lastSeen': DateTime.now().millisecondsSinceEpoch,
      'lastUpdatedBy': 'app',
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("Your Plugs"),
        actions: [
          IconButton(
            onPressed: () => FirebaseAuth.instance.signOut(),
            icon: const Icon(Icons.logout),
          ),
        ],
      ),
      drawer: Drawer(
        child: ListView(
          padding: EdgeInsets.zero,
          children: [
            const DrawerHeader(
              child: Text("Welcome!"),
              decoration: BoxDecoration(color: Colors.blue),
            ),
            ListTile(
              title: const Text('User ID'),
              subtitle: Text(uid),
              trailing: IconButton(
                icon: const Icon(Icons.copy),
                onPressed: () {
                  Clipboard.setData(ClipboardData(text: uid));
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(
                      content: Text("User ID copied to clipboard"),
                    ),
                  );
                },
              ),
            ),
            ListTile(
              title: const Text('Logout'),
              leading: const Icon(Icons.logout),
              onTap: () {
                FirebaseAuth.instance.signOut();
                Navigator.pop(context);
              },
            ),
          ],
        ),
      ),
      body: StreamBuilder<DatabaseEvent>(
        stream: plugsRef.child("users/$uid/plugs").onValue,
        builder: (context, snapshot) {
          if (!snapshot.hasData) {
            return const Center(child: CircularProgressIndicator());
          }

          final plugsData = snapshot.data!.snapshot.value as Map?;
          if (plugsData == null || plugsData.isEmpty) {
            return const Center(child: Text("No plugs yet! Add one."));
          }

          final plugWidgets =
              plugsData.entries
                  .where((entry) {
                    final data = Map<String, dynamic>.from(entry.value);
                    return data['isDeleted'] != true;
                  })
                  .map((entry) {
                    final data = Map<String, dynamic>.from(entry.value);
                    return ListTile(
                      title: Text(data['name'] ?? "Unnamed Plug"),
                      subtitle: Text(
                        "Status: ${data['isOn'] == true ? "On" : "Off"}, Paused: ${data['isPaused'] == true ? "Yes" : "No"}",
                      ),
                      trailing: const Icon(Icons.arrow_forward_ios),
                      onTap: () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder:
                                (_) => PlugControlScreen(
                                  plugId: entry.key,
                                  plugData: data,
                                ),
                          ),
                        );
                      },
                    );
                  })
                  .toList();

          return ListView(children: plugWidgets);
        },
      ),
    );
  }
}
