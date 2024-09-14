import 'dart:async';

import 'package:flutter/material.dart';
import 'package:libmoc/libmoc.dart' as libmoc;
import 'package:path_provider/path_provider.dart';


void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  late int sumResult;
  //late Future<int> sumAsyncResult;
  late Future<String> mocstring;
  String asyncString = "aaaaa";
  //String filestring = libmoc.fileTest();


  @override
  void initState() {
    super.initState();
    sumResult = libmoc.sum(1, 2);
    libmoc.mocInit();
    //sumAsyncResult = libmoc.sumAsync(3, 4);
    mocstring = libmoc.mocDiscovery();
  }

  @override
  Widget build(BuildContext context) {
    const textStyle = TextStyle(fontSize: 25);
    const spacerSmall = SizedBox(height: 10);
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('Native Packages'),
        ),
        body: SingleChildScrollView(
          child: Container(
            padding: const EdgeInsets.all(10),
            child: Column(
              children: [
                const Text(
                  'This calls a native function through FFI that is shipped as source in the package. '
                  'The native codexx is built as part of the Flutter Runner build.',
                  style: textStyle,
                  textAlign: TextAlign.center,
                ),
                //spacerSmall,
                //Text(filestring),
                spacerSmall,
                Text(asyncString),
                IconButton(
                  icon: const Icon(Icons.refresh),
                  onPressed: () async {
                    //final stringa = await libmoc.libmocDiscover();
                    //String stringa = await libmoc.mocDiscover();
                    final directory = await getApplicationDocumentsDirectory();
                    String filecontent = libmoc.fileTest(directory.path);
                    setState(() {
                      asyncString = filecontent;
                    });
                  },
                ),
                spacerSmall,
                FutureBuilder<String>(
                  future: mocstring,
                  builder: (BuildContext context, AsyncSnapshot<String> value) {
                    final displayValue =
                        (value.hasData) ? value.data : 'loading';
                    return Text(
                      'await moc string() = $displayValue',
                      style: textStyle,
                      textAlign: TextAlign.center,
                    );
                  },
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
