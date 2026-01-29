import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:timer_app/screens/login_screen.dart';
import 'package:timer_app/screens/plug_list_screen.dart';

bool isPhoneOnline = false;
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  Firebase.initializeApp().then((_) {
    FirebaseDatabase.instance.ref(".info/connected").onValue.listen((event) {
      isPhoneOnline = event.snapshot.value == true;
      print("Phone online: $isPhoneOnline");
    });
    runApp(const MyApp());
  });
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Smart Plug App',
      theme: ThemeData(primarySwatch: Colors.teal),
      home: const AuthGate(),
    );
  }
}

class AuthGate extends StatelessWidget {
  const AuthGate({super.key});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<User?>(
      stream: FirebaseAuth.instance.authStateChanges(),
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Scaffold(
            body: Center(child: CircularProgressIndicator()),
          );
        }
        if (snapshot.hasData) {
          return const PlugListScreen();
        }
        return const LoginScreen();
      },
    );
  }
}
